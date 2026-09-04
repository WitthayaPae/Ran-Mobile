//  The mixer. See audio_mix.h for what it is and what it is not.
#include "audio_mix.h"
#include "ran_plat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <vector>
#include <string>

#define LOGI(...) RanPlat_Log(RANLOG_INFO,  "RanAudio", __VA_ARGS__)
#define LOGE(...) RanPlat_Log(RANLOG_ERROR, "RanAudio", __VA_ARGS__)

//  The path resolver: sound files live wherever the data root is, and the
//  client asks for them with the paths a PC install would have.
extern "C" FILE *ran_fopen ( const char *path, const char *mode );

namespace {

struct Clip {
    std::string name;            //  basename, for the report - a voice list of
                                 //  numbers says nothing about what is playing
    std::vector<short> pcm;      //  interleaved, always 2 channels after load
    int      channels;           //  what the file had, kept for byte maths
    int      bits;
    unsigned rate;
    unsigned bytesPerFrameSrc;   //  in the file's own format
    int      refs;
};

struct Voice {
    int      clip;               //  0 for a ring voice
    bool     used, playing, loop;
    double   pos;                //  frames into the clip, fractional for resampling
    float    gainL, gainR;
    long     volume, pan;
    unsigned freq;               //  0 = the clip's own rate

    //  Ring voices: memory the client writes into behind our back.
    std::vector<unsigned char> ring;
    int      ringChannels, ringBits;
    unsigned ringRate;
};

pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
std::vector<Clip *>  g_clips;    //  index 0 unused, so 0 can mean "none"
std::vector<Voice>   g_voices;
bool     g_ready = false;
bool     g_muted = false;
unsigned g_played = 0, g_underruns = 0;
//  Frames the sink has taken but not yet played. See RanAudio_SetSinkLatency.
int      g_sinkLatency = 0;
//  When the last mix block was produced, and how many frames it covered.
//
//  A hardware play cursor moves sample by sample. Ours only moves when the sink
//  asks for a block, 20 ms at a time, and a caller polling at the same period
//  can see it standing still. The music streamer reads a stalled cursor as "a
//  whole lap has been consumed" and refills the ENTIRE buffer - six seconds of
//  music rewritten under the playhead, which is the tearing and repeating.
//  So the reported position is interpolated between blocks, against the clock.
double   g_blockTime = 0.0;
int      g_blockFrames = 0;

//  DirectSound gives volume in hundredths of a decibel, 0 loudest,
//  -10000 silent. Anything at or below the floor is off, not very quiet.
float gainOf ( long centibels )
{
    if (centibels <= -10000) return 0.0f;
    if (centibels >= 0)      return 1.0f;
    return (float) pow ( 10.0, (double) centibels / 2000.0 );
}

//  Pan in the same units: negative is left. DirectSound attenuates the far
//  channel rather than boosting the near one, so full pan is one channel off.
void panOf ( long pan, float *l, float *r )
{
    *l = *r = 1.0f;
    if (pan > 0)      *l = gainOf ( -pan );
    else if (pan < 0) *r = gainOf (  pan );
}

void applyGains ( Voice &v )
{
    float l, r;
    panOf ( v.pan, &l, &r );
    const float g = gainOf ( v.volume );
    v.gainL = g * l;
    v.gainR = g * r;
}

Clip *clipOf ( int id )
{
    if (id <= 0 || (size_t) id >= g_clips.size()) return NULL;
    return g_clips[id];
}

Voice *voiceOf ( int id )
{
    if (id <= 0 || (size_t) id > g_voices.size()) return NULL;
    Voice &v = g_voices[id - 1];
    return v.used ? &v : NULL;
}

// ------------------------------------------------------------------- WAV

//  A RIFF reader that believes nothing: every chunk header is checked against
//  the bytes actually present. These files are content, and content is the
//  thing most likely to be truncated by a patch that went wrong.
bool decodeWav ( const unsigned char *p, size_t n, Clip &out )
{
    if (n < 44 || memcmp ( p, "RIFF", 4 ) != 0 || memcmp ( p + 8, "WAVE", 4 ) != 0)
        return false;

    size_t at = 12;
    int channels = 0, bits = 0, format = 0;
    unsigned rate = 0;
    const unsigned char *data = NULL;
    size_t dataBytes = 0;

    while (at + 8 <= n) {
        const char *id = (const char *) ( p + at );
        unsigned len = (unsigned) p[at+4] | ((unsigned) p[at+5] << 8) |
                       ((unsigned) p[at+6] << 16) | ((unsigned) p[at+7] << 24);
        const size_t body = at + 8;
        if (body + len > n) len = (unsigned) ( n - body );   //  truncated: take what is there

        if (memcmp ( id, "fmt ", 4 ) == 0 && len >= 16) {
            format   = p[body] | (p[body+1] << 8);
            channels = p[body+2] | (p[body+3] << 8);
            rate     = (unsigned) p[body+4] | ((unsigned) p[body+5] << 8) |
                       ((unsigned) p[body+6] << 16) | ((unsigned) p[body+7] << 24);
            bits     = p[body+14] | (p[body+15] << 8);
        } else if (memcmp ( id, "data", 4 ) == 0) {
            data = p + body;
            dataBytes = len;
        }
        at = body + len + ( len & 1 );      //  chunks are word aligned
    }

    if (!data || !channels || !rate) return false;
    if (format != 1 && format != 3 && format != 0xFFFE) return false;   //  PCM, float, extensible
    if (bits != 8 && bits != 16 && bits != 24 && bits != 32) return false;

    const size_t srcFrame = (size_t) channels * ( bits / 8 );
    const size_t frames = srcFrame ? dataBytes / srcFrame : 0;
    if (!frames) return false;

    out.channels = channels;
    out.bits = bits;
    out.rate = rate;
    out.bytesPerFrameSrc = (unsigned) srcFrame;
    out.pcm.resize ( frames * 2 );

    //  Everything is widened to 16-bit stereo here, once, so the mix loop has
    //  exactly one case to handle however varied the content is.
    for (size_t f = 0; f < frames; ++f) {
        for (int c = 0; c < 2; ++c) {
            const int sc = ( c < channels ) ? c : channels - 1;
            const unsigned char *s = data + f * srcFrame + (size_t) sc * ( bits / 8 );
            int v = 0;
            switch (bits) {
                case 8:  v = ( (int) *s - 128 ) << 8; break;
                case 16: v = (short) ( s[0] | ( s[1] << 8 ) ); break;
                case 24: v = (int) ( ( s[1] | ( s[2] << 8 ) ) ) - ( ( s[2] & 0x80 ) ? 0x10000 : 0 ); break;
                case 32:
                    if (format == 3) {
                        float fv; memcpy ( &fv, s, 4 );
                        if (fv >  1.0f) fv =  1.0f;
                        if (fv < -1.0f) fv = -1.0f;
                        v = (int) ( fv * 32767.0f );
                    } else {
                        int iv; memcpy ( &iv, s, 4 );
                        v = iv >> 16;
                    }
                    break;
            }
            out.pcm[f * 2 + c] = (short) v;
        }
    }
    return true;
}

int addClip ( Clip *c )
{
    pthread_mutex_lock ( &g_lock );
    if (g_clips.empty()) g_clips.push_back ( NULL );     //  reserve 0
    for (size_t i = 1; i < g_clips.size(); ++i) {
        if (!g_clips[i]) { g_clips[i] = c; pthread_mutex_unlock ( &g_lock ); return (int) i; }
    }
    g_clips.push_back ( c );
    const int id = (int) g_clips.size() - 1;
    pthread_mutex_unlock ( &g_lock );
    return id;
}

} // namespace

// ------------------------------------------------------------------ public

extern "C" int RanAudio_Init ( void )
{
    pthread_mutex_lock ( &g_lock );
    if (g_clips.empty()) g_clips.push_back ( NULL );
    g_ready = true;
    pthread_mutex_unlock ( &g_lock );
    LOGI ( "mixer up: %d Hz, %d channels", RANAUDIO_RATE, RANAUDIO_CHANNELS );
    return 1;
}

extern "C" void RanAudio_Shutdown ( void )
{
    pthread_mutex_lock ( &g_lock );
    g_ready = false;
    for (size_t i = 0; i < g_clips.size(); ++i) { delete g_clips[i]; g_clips[i] = NULL; }
    g_clips.clear();
    g_voices.clear();
    pthread_mutex_unlock ( &g_lock );
}

extern "C" int RanAudio_LoadWavMemory ( const void *data, size_t size )
{
    if (!data || size < 44) return 0;
    Clip *c = new Clip();
    c->refs = 1;
    if (!decodeWav ( (const unsigned char *) data, size, *c )) { delete c; return 0; }
    return addClip ( c );
}

extern "C" int RanAudio_LoadWav ( const char *path )
{
    if (!path || !*path) return 0;
    FILE *f = ran_fopen ( path, "rb" );
    if (!f) return 0;

    fseek ( f, 0, SEEK_END );
    const long n = ftell ( f );
    fseek ( f, 0, SEEK_SET );
    if (n <= 44) { fclose ( f ); return 0; }

    std::vector<unsigned char> buf ( (size_t) n );
    const size_t got = fread ( &buf[0], 1, (size_t) n, f );
    fclose ( f );
    if (got != (size_t) n) return 0;

    const int id = RanAudio_LoadWavMemory ( &buf[0], got );
    if (id) {
        const char *slash = strrchr ( path, 47 );
        const char *back  = strrchr ( path, 92 );
        if (back > slash) slash = back;
        pthread_mutex_lock ( &g_lock );
        Clip *c = clipOf ( id );
        if (c) c->name = slash ? slash + 1 : path;
        pthread_mutex_unlock ( &g_lock );
    }
    if (!id) {
        //  Named once each: a sound that will not decode is content, and the
        //  file name is the only thing that identifies it.
        static int s_said = 0;
        if (s_said < 16) { ++s_said; LOGE ( "cannot decode %s", path ); }
    }
    return id;
}

extern "C" void RanAudio_ReleaseClip ( int clip )
{
    pthread_mutex_lock ( &g_lock );
    Clip *c = clipOf ( clip );
    if (c && --c->refs <= 0) { delete c; g_clips[clip] = NULL; }
    pthread_mutex_unlock ( &g_lock );
}

extern "C" int RanAudio_ClipFrames ( int clip )
{
    pthread_mutex_lock ( &g_lock );
    Clip *c = clipOf ( clip );
    const int n = c ? (int) ( c->pcm.size() / 2 ) : 0;
    pthread_mutex_unlock ( &g_lock );
    return n;
}

extern "C" int RanAudio_VoiceCreate ( int clip )
{
    pthread_mutex_lock ( &g_lock );
    Clip *c = clipOf ( clip );
    if (c) ++c->refs;

    int id = 0;
    for (size_t i = 0; i < g_voices.size(); ++i) {
        if (!g_voices[i].used) { id = (int) i + 1; break; }
    }
    if (!id) { g_voices.push_back ( Voice() ); id = (int) g_voices.size(); }

    Voice &v = g_voices[id - 1];
    v = Voice();
    v.used = true;
    v.clip = clip;
    v.volume = 0;
    v.pan = 0;
    applyGains ( v );
    pthread_mutex_unlock ( &g_lock );
    return id;
}

extern "C" int RanAudio_RingCreate ( unsigned bytes, int channels, int bitsPerSample, unsigned rate )
{
    if (!bytes) return 0;
    const int id = RanAudio_VoiceCreate ( 0 );
    if (!id) return 0;
    pthread_mutex_lock ( &g_lock );
    Voice &v = g_voices[id - 1];
    v.ring.assign ( bytes, 0 );
    v.ringChannels = channels > 0 ? channels : 2;
    v.ringBits = bitsPerSample > 0 ? bitsPerSample : 16;
    v.ringRate = rate ? rate : RANAUDIO_RATE;
    pthread_mutex_unlock ( &g_lock );
    return id;
}

extern "C" void *RanAudio_RingData ( int voice )
{
    pthread_mutex_lock ( &g_lock );
    Voice *v = voiceOf ( voice );
    void *p = ( v && !v->ring.empty() ) ? (void *) &v->ring[0] : NULL;
    pthread_mutex_unlock ( &g_lock );
    return p;
}

extern "C" unsigned RanAudio_RingBytes ( int voice )
{
    pthread_mutex_lock ( &g_lock );
    Voice *v = voiceOf ( voice );
    const unsigned n = v ? (unsigned) v->ring.size() : 0;
    pthread_mutex_unlock ( &g_lock );
    return n;
}

extern "C" void RanAudio_VoiceDestroy ( int voice )
{
    pthread_mutex_lock ( &g_lock );
    Voice *v = voiceOf ( voice );
    if (v) {
        Clip *c = clipOf ( v->clip );
        if (c && --c->refs <= 0) { delete c; g_clips[v->clip] = NULL; }
        *v = Voice();
    }
    pthread_mutex_unlock ( &g_lock );
}

extern "C" void RanAudio_VoicePlay ( int voice, int loop )
{
    pthread_mutex_lock ( &g_lock );
    Voice *v = voiceOf ( voice );
    if (v) {
        v->loop = ( loop != 0 );
        v->playing = true;
        //  A clip starts from its beginning; a RING does not.
        //
        //  The ring is a stream the client is writing into continuously, and
        //  its play cursor is the only thing telling the writer how far it has
        //  got. Rewinding it to 0 on Play makes the music jump back into a part
        //  of the buffer that holds a different second of the track - which is
        //  the music tearing and repeating itself.
        if (v->ring.empty()) v->pos = 0.0;
        ++g_played;
        //  Every start, by name, so a sound the engine asks for and never
        //  hears can be told from one it never asks for at all.
        if (RanPlat_DiagExists ( "audiolog" )) {
            Clip *c = clipOf ( v->clip );
            LOGI ( "play %s%s vol %ld",
                   !v->ring.empty() ? "bgm-ring" : ( c && !c->name.empty() ? c->name.c_str() : "?" ),
                   v->loop ? " (loop)" : "", v->volume );
        }
    }
    pthread_mutex_unlock ( &g_lock );
}

extern "C" void RanAudio_VoiceStop ( int voice )
{
    pthread_mutex_lock ( &g_lock );
    Voice *v = voiceOf ( voice );
    if (v) v->playing = false;
    pthread_mutex_unlock ( &g_lock );
}

extern "C" int RanAudio_VoiceIsPlaying ( int voice )
{
    pthread_mutex_lock ( &g_lock );
    Voice *v = voiceOf ( voice );
    const int on = ( v && v->playing ) ? 1 : 0;
    pthread_mutex_unlock ( &g_lock );
    return on;
}

extern "C" void RanAudio_VoiceSetVolume ( int voice, long centibels )
{
    pthread_mutex_lock ( &g_lock );
    Voice *v = voiceOf ( voice );
    if (v) { v->volume = centibels; applyGains ( *v ); }
    pthread_mutex_unlock ( &g_lock );
}

extern "C" void RanAudio_VoiceSetPan ( int voice, long centibels )
{
    pthread_mutex_lock ( &g_lock );
    Voice *v = voiceOf ( voice );
    if (v) { v->pan = centibels; applyGains ( *v ); }
    pthread_mutex_unlock ( &g_lock );
}

extern "C" void RanAudio_VoiceSetFrequency ( int voice, unsigned hz )
{
    pthread_mutex_lock ( &g_lock );
    Voice *v = voiceOf ( voice );
    if (v) v->freq = hz;
    pthread_mutex_unlock ( &g_lock );
}

extern "C" void RanAudio_VoiceSetPosition ( int voice, unsigned byteOffset )
{
    pthread_mutex_lock ( &g_lock );
    Voice *v = voiceOf ( voice );
    if (v) {
        const unsigned frameBytes = !v->ring.empty()
            ? (unsigned) ( v->ringChannels * ( v->ringBits / 8 ) )
            : ( clipOf ( v->clip ) ? clipOf ( v->clip )->bytesPerFrameSrc : 4 );
        v->pos = frameBytes ? (double) byteOffset / frameBytes : 0.0;
    }
    pthread_mutex_unlock ( &g_lock );
}

extern "C" unsigned RanAudio_VoicePosition ( int voice )
{
    pthread_mutex_lock ( &g_lock );
    Voice *v = voiceOf ( voice );
    unsigned at = 0;
    if (v) {
        const unsigned frameBytes = !v->ring.empty()
            ? (unsigned) ( v->ringChannels * ( v->ringBits / 8 ) )
            : ( clipOf ( v->clip ) ? clipOf ( v->clip )->bytesPerFrameSrc : 4 );

        //  Held back by what the sink still holds, and carried forward from the
        //  last block by the clock so it never appears to stand still between
        //  blocks - which is what a hardware cursor does.
        double ahead = 0.0;
        if (g_blockTime > 0.0) {
            struct timespec ts;
            clock_gettime ( CLOCK_MONOTONIC, &ts );
            const double dt = ( (double) ts.tv_sec + (double) ts.tv_nsec * 1e-9 ) - g_blockTime;
            ahead = dt * (double) RANAUDIO_RATE;
            if (ahead < 0.0) ahead = 0.0;
            if (ahead > (double) g_blockFrames) ahead = (double) g_blockFrames;
        }
        double pos = v->pos - (double) g_sinkLatency + ahead;
        if (!v->ring.empty()) {
            const double ringFrames = (double) ( v->ring.size() / ( frameBytes ? frameBytes : 1 ) );
            while (pos < 0.0) pos += ringFrames;
        } else if (pos < 0.0) pos = 0.0;

        at = (unsigned) ( pos * frameBytes );
    }
    pthread_mutex_unlock ( &g_lock );
    return at;
}

//  882 frames is the sink block on both platforms; see audio_opensl.cpp and
//  audio_audioqueue.mm.
extern "C" int RanAudio_SafetyFrames ( void ) { return 882 * 3; }

extern "C" void RanAudio_SetSinkLatency ( int frames )
{
    g_sinkLatency = frames > 0 ? frames : 0;
    LOGI ( "sink holds %d frames (%.0f ms) - the reported play cursor lags by that much",
           g_sinkLatency, g_sinkLatency * 1000.0 / RANAUDIO_RATE );
}

extern "C" void RanAudio_SetMuted ( int muted ) { g_muted = ( muted != 0 ); }

extern "C" void RanAudio_LogStats ( void )
{
    LOGI ( "audio - %u voices started, %u underruns", g_played, g_underruns );
}

// --------------------------------------------------------------------- mix

extern "C" void RanAudio_Mix ( short *out, int frames )
{
    {
        struct timespec ts;
        clock_gettime ( CLOCK_MONOTONIC, &ts );
        g_blockTime = (double) ts.tv_sec + (double) ts.tv_nsec * 1e-9;
        g_blockFrames = frames;
    }
    memset ( out, 0, (size_t) frames * 2 * sizeof(short) );
    if (!g_ready || g_muted) return;

    //  try-lock, never lock: this runs on the audio thread against a deadline.
    //  A frame of silence is a click; a frame that arrives late is a stutter,
    //  and the stutter is worse.
    if (pthread_mutex_trylock ( &g_lock ) != 0) { ++g_underruns; return; }

    //  Mixed into an int accumulator and clamped once at the end: summing
    //  shorts in place clips every voice against the last, which is audible as
    //  distortion the moment two loud sounds overlap.
    std::vector<int> acc ( (size_t) frames * 2, 0 );

    for (size_t vi = 0; vi < g_voices.size(); ++vi) {
        Voice &v = g_voices[vi];
        if (!v.used || !v.playing) continue;

        if (!v.ring.empty()) {
            //  A ring voice: read straight out of the client's buffer, wrapping.
            const int fb = v.ringChannels * ( v.ringBits / 8 );
            if (fb <= 0) continue;
            const double step = (double) v.ringRate / (double) RANAUDIO_RATE;
            const size_t ringFrames = v.ring.size() / fb;
            if (!ringFrames) continue;

            for (int f = 0; f < frames; ++f) {
                size_t i = (size_t) v.pos;
                if (i >= ringFrames) { i %= ringFrames; v.pos = (double) i; }
                const unsigned char *s = &v.ring[i * fb];
                int l = 0, r = 0;
                if (v.ringBits == 16) {
                    l = (short) ( s[0] | ( s[1] << 8 ) );
                    r = ( v.ringChannels > 1 ) ? (short) ( s[2] | ( s[3] << 8 ) ) : l;
                } else {
                    l = ( (int) s[0] - 128 ) << 8;
                    r = ( v.ringChannels > 1 ) ? ( ( (int) s[1] - 128 ) << 8 ) : l;
                }
                acc[f * 2]     += (int) ( l * v.gainL );
                acc[f * 2 + 1] += (int) ( r * v.gainR );
                v.pos += step;
            }
            continue;
        }

        Clip *c = clipOf ( v.clip );
        if (!c || c->pcm.empty()) { v.playing = false; continue; }

        const size_t clipFrames = c->pcm.size() / 2;
        const unsigned srcRate = v.freq ? v.freq : c->rate;
        const double step = (double) srcRate / (double) RANAUDIO_RATE;

        for (int f = 0; f < frames; ++f) {
            size_t i = (size_t) v.pos;
            if (i >= clipFrames) {
                if (!v.loop) { v.playing = false; v.pos = 0.0; break; }
                i %= clipFrames;
                v.pos = (double) i;
            }
            //  Linear between neighbours, so a clip played at a shifted pitch
            //  does not buzz. The step is near 1 for this content, which is why
            //  nothing fancier is warranted.
            const size_t j = ( i + 1 < clipFrames ) ? i + 1 : ( v.loop ? 0 : i );
            const double frac = v.pos - (double) i;
            const int l = (int) ( c->pcm[i*2]   + ( c->pcm[j*2]   - c->pcm[i*2]   ) * frac );
            const int r = (int) ( c->pcm[i*2+1] + ( c->pcm[j*2+1] - c->pcm[i*2+1] ) * frac );
            acc[f * 2]     += (int) ( l * v.gainL );
            acc[f * 2 + 1] += (int) ( r * v.gainR );
            v.pos += step;
        }
    }

    int voicesOn = 0;
    for (size_t vi = 0; vi < g_voices.size(); ++vi)
        if (g_voices[vi].used && g_voices[vi].playing) ++voicesOn;

    //  The sum of the voices, before anything is done about it.
    int rawPeak = 0;
    for (size_t i = 0; i < acc.size(); ++i) {
        const int m = acc[i] < 0 ? -acc[i] : acc[i];
        if (m > rawPeak) rawPeak = m;
    }

    //  A limiter, because a fixed-point sum has nowhere to put the overshoot.
    //
    //  The client mixes at unity: music sits at 0 dB (DSBVOLUME_MAX) and a
    //  nearby ambient loop does too, so the sum saturates and every sound
    //  smears into every other one - which is exactly what "it mixes everything
    //  together" sounds like. DirectSound has a wider intermediate and the OS
    //  mixer below it; this does not, so the headroom has to be made here.
    //
    //  Gain follows the block peak: pulled down at once when it would clip,
    //  released slowly so a single loud hit does not duck the whole scene.
    //  Nothing is attenuated while the mix fits, so a quiet scene is untouched.
    static float s_gain = 1.0f;
    const bool limiterOff = RanPlat_DiagExists ( "nolimiter" ) != 0;
    const float kCeiling = 32000.0f;
    float target = 1.0f;
    if (!limiterOff && rawPeak > kCeiling) target = kCeiling / (float) rawPeak;

    int peak = 0, clipped = 0;
    for (size_t i = 0; i < acc.size(); ++i) {
        //  Per sample, so the gain change is inaudible: fast down, slow up.
        s_gain += ( target < s_gain ? 0.25f : 0.0005f ) * ( target - s_gain );
        int s = (int) ( acc[i] * s_gain );
        if (s >  32767) { s =  32767; ++clipped; }
        if (s < -32768) { s = -32768; ++clipped; }
        out[i] = (short) s;
        const int m = s < 0 ? -s : s;
        if (m > peak) peak = m;
    }

    //  Proof that sound is actually reaching the device, which is otherwise
    //  unanswerable without ears: how many voices are live and how loud the
    //  buffer that just went out was. Once every few seconds, and only while
    //  something is playing.
    {
        static unsigned s_blocks = 0, s_peak = 0, s_maxVoices = 0;
        static unsigned s_clipped = 0, s_over = 0;
        ++s_blocks;
        s_clipped += (unsigned) clipped;
        if (rawPeak > 32767) ++s_over;
        if ((unsigned) peak > s_peak) s_peak = (unsigned) peak;
        if ((unsigned) voicesOn > s_maxVoices) s_maxVoices = (unsigned) voicesOn;
        //  50 blocks of 882 frames is a second at 44.1 kHz.
        if (s_blocks >= 250) {
            if (s_maxVoices) {
                //  Which voices, not just how many: six things playing while
                //  standing still is only a problem if they are the wrong six.
                std::string who;
                int listed = 0;
                for (size_t vi = 0; vi < g_voices.size() && listed < 10; ++vi) {
                    const Voice &v = g_voices[vi];
                    if (!v.used || !v.playing) continue;
                    Clip *c = clipOf ( v.clip );
                    char tmp[128];
                    snprintf ( tmp, sizeof(tmp), "%s%s%s(%ld)",
                               listed ? ", " : "",
                               !v.ring.empty() ? "bgm-ring" : ( c && !c->name.empty() ? c->name.c_str() : "?" ),
                               v.loop ? " loop" : "", v.volume );
                    who += tmp;
                    ++listed;
                }
                LOGI ( "out: %u voices, peak %u/32767, %u blocks over full scale, "
                       "%u clipped samples, gain %.2f [%s]",
                       s_maxVoices, s_peak, s_over, s_clipped, s_gain, who.c_str() );
            }
            s_blocks = 0; s_peak = 0; s_maxVoices = 0; s_clipped = 0; s_over = 0;
        }
    }

    //  With the "audiodump" flag present, the first ten seconds of what the
    //  mixer actually produced are written to the diagnostic directory as raw
    //  16-bit stereo PCM. Listening is the only way to settle what a defect
    //  sounds like, and this is the closest thing to listening that a log can
    //  do: the file can be pulled and measured.
    {
        static FILE *s_dump = NULL;
        static long  s_left = 0;
        static bool  s_armed = false;
        const bool on = RanPlat_DiagExists ( "audiodump" ) != 0;
        if (on && !s_armed) {
            s_armed = true;
            s_dump = RanPlat_DiagOpenWrite ( "mix.pcm" );
            s_left = (long) RANAUDIO_RATE * 10;          //  ten seconds of frames
            if (s_dump) LOGI ( "audio dump started" );
        } else if (!on && s_armed) {
            s_armed = false;
            if (s_dump) { fclose ( s_dump ); s_dump = NULL; LOGI ( "audio dump closed" ); }
        }
        if (s_dump && s_left > 0) {
            const long n = frames < s_left ? frames : s_left;
            fwrite ( out, sizeof(short) * 2, (size_t) n, s_dump );
            s_left -= n;
            if (s_left <= 0) { fclose ( s_dump ); s_dump = NULL; LOGI ( "audio dump complete" ); }
        }
    }

    pthread_mutex_unlock ( &g_lock );
}
