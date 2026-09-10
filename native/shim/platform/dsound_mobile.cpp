//  IDirectSound / IDirectSoundBuffer, on top of the mixer.
//
//  The music path does not go through dsutil at all: BgmSound calls
//  DirectSoundCreate8, creates a streaming buffer, and writes decoded OGG into
//  it with Lock/Unlock while watching GetCurrentPosition. So the buffer has to
//  be a real ring with a real play cursor, not a stub - which is exactly what a
//  ring voice in the mixer is.
#include "windows.h"
#include "ran_plat.h"
#include "audio_mix.h"

#include <dsound.h>
#include <string.h>

#define LOGI(...) RanPlat_Log(RANLOG_INFO,  "RanAudio", __VA_ARGS__)
#define LOGE(...) RanPlat_Log(RANLOG_ERROR, "RanAudio", __VA_ARGS__)

namespace {

class RanSoundBuffer : public IDirectSoundBuffer
{
public:
    LONG      m_ref;
    int       m_voice;
    unsigned  m_bytes;
    WAVEFORMATEX m_fmt;
    DWORD     m_flags;

    RanSoundBuffer ( int voice, unsigned bytes, const WAVEFORMATEX &fmt, DWORD flags )
        : m_ref(1), m_voice(voice), m_bytes(bytes), m_fmt(fmt), m_flags(flags) {}

    ~RanSoundBuffer () { if (m_voice) RanAudio_VoiceDestroy ( m_voice ); }

    //  IUnknown
    STDMETHOD(QueryInterface)(REFIID, LPVOID *ppv) { if (ppv) *ppv = this; ++m_ref; return S_OK; }
    STDMETHOD_(ULONG,AddRef)() { return (ULONG) ++m_ref; }
    STDMETHOD_(ULONG,Release)()
    {
        const LONG r = --m_ref;
        if (r <= 0) { delete this; return 0; }
        return (ULONG) r;
    }

    STDMETHOD(GetCaps)(LPDSBCAPS pCaps)
    {
        if (!pCaps) return DSERR_INVALIDPARAM;
        const DWORD size = pCaps->dwSize;
        memset ( pCaps, 0, size );
        pCaps->dwSize = size;
        pCaps->dwFlags = m_flags;
        pCaps->dwBufferBytes = m_bytes;
        return DS_OK;
    }

    STDMETHOD(GetCurrentPosition)(LPDWORD pPlay, LPDWORD pWrite)
    {
        unsigned at = m_voice ? RanAudio_VoicePosition ( m_voice ) : 0;
        //  DirectSound reports both cursors on a sample-frame boundary. Holding
        //  to that here means no caller can ever be handed a partial frame.
        if (m_fmt.nBlockAlign) at -= at % m_fmt.nBlockAlign;
        //  Every step the caller sees. A cursor that ever moves BACKWARDS
        //  without wrapping makes the streamer think a lap has passed and refill
        //  the whole buffer, which is the music racing through the track.
        if (m_voice && m_bytes) {
            static unsigned s_prev = 0; static bool s_have = false;
            static unsigned s_back = 0, s_calls = 0, s_wraps = 0;
            if (s_have) {
                ++s_calls;
                if (at < s_prev) {
                    //  A real wrap lands near the start after being near the end.
                    if (s_prev > m_bytes - m_bytes / 8 && at < m_bytes / 8) ++s_wraps;
                    else {
                        ++s_back;
                        if (s_back <= 5 && RanPlat_DiagExists ( "audiolog" ))
                            LOGI ( "cursor went BACKWARDS: %u -> %u", s_prev, at );
                    }
                }
                if (s_calls % 300 == 0 && RanPlat_DiagExists ( "audiolog" ))
                    LOGI ( "cursor: %u reads, %u wraps, %u backward steps", s_calls, s_wraps, s_back );
            }
            s_prev = at; s_have = true;
        }
        if (pPlay)  *pPlay = at;
        //  The write cursor is the first byte it is safe to write, and
        //  everything between the play cursor and it is in flight.
        //
        //  This used to be an invented lead of 1024 frames - barely one mixer
        //  block - and 6% of the client streamer writes landed directly on the
        //  play cursor, i.e. on audio being mixed at that instant. That is the
        //  music smearing over itself. The lead now comes from the mixer, which
        //  is the only thing that knows how much it has committed.
        if (pWrite) *pWrite = m_bytes
            ? ( at + (unsigned) RanAudio_SafetyFrames() * m_fmt.nBlockAlign ) % m_bytes
            : 0;
        return DS_OK;
    }

    STDMETHOD(GetFormat)(LPWAVEFORMATEX pfmt, DWORD size, LPDWORD written)
    {
        if (pfmt && size >= sizeof(WAVEFORMATEX)) memcpy ( pfmt, &m_fmt, sizeof(m_fmt) );
        if (written) *written = sizeof(WAVEFORMATEX);
        return DS_OK;
    }

    STDMETHOD(GetVolume)(LPLONG pVol)     { if (pVol) *pVol = m_volume; return DS_OK; }
    STDMETHOD(GetPan)(LPLONG pPan)        { if (pPan) *pPan = m_pan;    return DS_OK; }
    STDMETHOD(GetFrequency)(LPDWORD pHz)  { if (pHz)  *pHz  = m_freq ? m_freq : m_fmt.nSamplesPerSec; return DS_OK; }

    STDMETHOD(GetStatus)(LPDWORD pStatus)
    {
        if (!pStatus) return DSERR_INVALIDPARAM;
        *pStatus = 0;
        if (m_voice && RanAudio_VoiceIsPlaying ( m_voice )) {
            *pStatus |= DSBSTATUS_PLAYING;
            if (m_looping) *pStatus |= DSBSTATUS_LOOPING;
        }
        return DS_OK;
    }

    STDMETHOD(Initialize)(LPDIRECTSOUND, LPCDSBUFFERDESC) { return DS_OK; }

    //  Lock hands out the ring itself. There is no separate staging copy: the
    //  mixer reads the same bytes, which is what makes a streaming buffer work
    //  without a copy per notification.
    STDMETHOD(Lock)(DWORD offset, DWORD bytes, LPVOID *ppv1, LPDWORD pb1,
                    LPVOID *ppv2, LPDWORD pb2, DWORD flags)
    {
        unsigned char *base = (unsigned char *) ( m_voice ? RanAudio_RingData ( m_voice ) : NULL );
        if (!base || !m_bytes) return DSERR_INVALIDPARAM;

        if (flags & DSBLOCK_ENTIREBUFFER) { offset = 0; bytes = m_bytes; }
        if (offset >= m_bytes) offset %= m_bytes;
        if (bytes > m_bytes) bytes = m_bytes;

        //  What the client asks for, and how often. One refill should be one
        //  timer tick of audio; anything else says the loop is not running at
        //  the rate it thinks it is.
        {
            static double s_last = 0.0, s_sum = 0.0, s_bytes = 0.0; static unsigned s_n = 0;
            struct timespec ts; clock_gettime ( CLOCK_MONOTONIC, &ts );
            const double now = ts.tv_sec + ts.tv_nsec * 1e-9;
            if (s_last > 0.0) { s_sum += now - s_last; s_bytes += bytes; ++s_n; }
            s_last = now;
            if (s_n >= 100 && RanPlat_DiagExists ( "audiolog" )) {
                LOGI ( "refill: every %.1f ms, %.0f bytes each (%.0f bytes/s, real time is %u)",
                       s_sum * 1000.0 / s_n, s_bytes / s_n,
                       s_bytes / ( s_sum > 0 ? s_sum : 1 ), (unsigned) m_fmt.nAvgBytesPerSec );
                s_sum = 0.0; s_bytes = 0.0; s_n = 0;
            }
        }
        //  A raw trace of the first refills: where the client is writing, how
        //  much, and where the play cursor is at that moment.
        {
            static unsigned s_traced = 0;
            //  Every 50th refill for the whole session, not just the start: the
            //  first half second is healthy and the fault appears later.
            if (( ++s_traced % 50 ) == 0 && RanPlat_DiagExists ( "audiolog" )) {
                const unsigned pl = m_voice ? RanAudio_VoicePosition ( m_voice ) : 0;
                LOGI ( "lock #%u: writeCursor %u + %u bytes, play at %u, gap %u",
                       s_traced, offset, bytes, pl,
                       ( pl + m_bytes - offset ) % m_bytes );
            }
        }
        m_lockOffset = offset;
        m_lockBytes  = bytes;
        const DWORD first = ( offset + bytes <= m_bytes ) ? bytes : ( m_bytes - offset );
        if (ppv1) *ppv1 = base + offset;
        if (pb1)  *pb1  = first;
        //  The wrap half, which the caller is entitled to and will write to.
        if (ppv2) *ppv2 = ( bytes > first ) ? base : NULL;
        if (pb2)  *pb2  = bytes - first;
        return DS_OK;
    }

    STDMETHOD(Play)(DWORD, DWORD, DWORD flags)
    {
        if (RanPlat_DiagExists ( "audiolog" ))
            LOGI ( "buffer play: voice %d, flags %08x", m_voice, (unsigned) flags );
        m_looping = ( flags & DSBPLAY_LOOPING ) != 0;
        if (m_voice) RanAudio_VoicePlay ( m_voice, m_looping ? 1 : 0 );
        return DS_OK;
    }

    STDMETHOD(SetCurrentPosition)(DWORD at)
    {
        if (m_voice) RanAudio_VoiceSetPosition ( m_voice, at );
        return DS_OK;
    }

    STDMETHOD(SetFormat)(LPCWAVEFORMATEX pfmt)
    {
        if (pfmt) memcpy ( &m_fmt, pfmt, sizeof(WAVEFORMATEX) );
        return DS_OK;
    }

    STDMETHOD(SetVolume)(LONG v)
    {
        if (v != m_volume && RanPlat_DiagExists ( "audiolog" ))
            LOGI ( "volume: voice %d -> %ld centibels", m_voice, (long) v );
        m_volume = v;
        if (m_voice) RanAudio_VoiceSetVolume ( m_voice, v );
        return DS_OK;
    }
    STDMETHOD(SetPan)(LONG p)
    {
        m_pan = p;
        if (m_voice) RanAudio_VoiceSetPan ( m_voice, p );
        return DS_OK;
    }
    STDMETHOD(SetFrequency)(DWORD hz)
    {
        m_freq = hz;
        if (m_voice) RanAudio_VoiceSetFrequency ( m_voice, hz );
        return DS_OK;
    }

    STDMETHOD(Stop)()
    {
        if (m_voice) RanAudio_VoiceStop ( m_voice );
        return DS_OK;
    }

    //  Nothing to do - the client wrote straight into the ring. The counting is
    //  the answer to "is the music actually being decoded into this buffer",
    //  which is otherwise unanswerable without ears.
    STDMETHOD(Unlock)(LPVOID p1, DWORD n1, LPVOID p2, DWORD n2)
    {
        //  Did this write land on top of what the mixer is playing right now?
        //
        //  The client streams into the ring while the mixer reads it, and the
        //  only thing keeping them apart is the play cursor this class reports.
        //  If a write covers the read position, the music plays part of one
        //  block and part of another - it smears over itself.
        if (m_voice && m_lockBytes) {
            const unsigned play = RanAudio_VoicePosition ( m_voice );
            const unsigned from = m_lockOffset;
            const unsigned to   = m_lockOffset + m_lockBytes;   //  may wrap
            const bool hit = ( to <= m_bytes )
                           ? ( play >= from && play < to )
                           : ( play >= from || play < ( to - m_bytes ) );
            static unsigned s_writes = 0, s_hits = 0;
            static unsigned s_minLead = 0xFFFFFFFF, s_starved = 0;
            ++s_writes;
            if (hit) ++s_hits;

            //  How much audio is ahead of the play head after this write.
            //
            //  The client fills from its own write cursor up to the play
            //  cursor, so what is ahead of the play head is everything else in
            //  the ring. If that ever falls to nothing the mixer is reading
            //  bytes the writer has not reached this lap - stale audio, then a
            //  gap: the music cutting out and repeating.
            const unsigned end = ( m_lockOffset + m_lockBytes ) % m_bytes;
            const unsigned lead = ( end + m_bytes - play ) % m_bytes;
            if (lead < s_minLead) s_minLead = lead;
            if (lead < m_fmt.nAvgBytesPerSec / 20) ++s_starved;   //  under 50 ms

            //  How fast the play cursor actually moves, against the clock.
            //  It should advance exactly nAvgBytesPerSec per second; the
            //  client sizes every refill from it, so if it runs fast the whole
            //  track is decoded in seconds.
            {
                static unsigned s_lastPlay = 0;
                static double   s_lastTime = 0.0;
                static double   s_bytes = 0.0;
                struct timespec ts; clock_gettime ( CLOCK_MONOTONIC, &ts );
                const double now = ts.tv_sec + ts.tv_nsec * 1e-9;
                if (s_lastTime > 0.0) {
                    s_bytes += (double) ( ( play + m_bytes - s_lastPlay ) % m_bytes );
                    if (now - s_lastTime > 2.0 && RanPlat_DiagExists ( "audiolog" )) {
                        LOGI ( "cursor: %.0f bytes/s (format says %u)",
                               s_bytes / ( now - s_lastTime ), (unsigned) m_fmt.nAvgBytesPerSec );
                        s_bytes = 0.0; s_lastTime = now;
                    }
                } else s_lastTime = now;
                s_lastPlay = play;
            }

            if (s_writes % 200 == 0 && RanPlat_DiagExists ( "audiolog" )) {
                LOGI ( "ring: %u writes, %u on the cursor, min lead %u bytes (%.0f ms), "
                       "%u writes under 50 ms of lead",
                       s_writes, s_hits, s_minLead,
                       m_fmt.nAvgBytesPerSec ? s_minLead * 1000.0 / m_fmt.nAvgBytesPerSec : 0.0,
                       s_starved );
                s_minLead = 0xFFFFFFFF;
            }
            m_lockBytes = 0;
        }
        static unsigned s_calls = 0, s_bytes = 0, s_nonzero = 0;
        ++s_calls;
        const unsigned char *parts[2] = { (const unsigned char *) p1, (const unsigned char *) p2 };
        const DWORD sizes[2] = { n1, n2 };
        for (int k = 0; k < 2; ++k) {
            if (!parts[k]) continue;
            s_bytes += sizes[k];
            for (DWORD i = 0; i < sizes[k]; ++i) if (parts[k][i]) { ++s_nonzero; }
        }
        if (s_calls % 200 == 0 && RanPlat_DiagExists ( "audiolog" ))
            LOGI ( "ring writes: %u calls, %u bytes, %u non-zero", s_calls, s_bytes, s_nonzero );
        return DS_OK;
    }
    STDMETHOD(Restore)() { return DS_OK; }

private:
    DWORD m_lockOffset = 0, m_lockBytes = 0;
    LONG  m_volume = 0, m_pan = 0;
    DWORD m_freq = 0;
    bool  m_looping = false;
};

class RanDirectSound : public IDirectSound8
{
public:
    LONG m_ref;
    RanDirectSound () : m_ref(1) {}

    STDMETHOD(QueryInterface)(REFIID, LPVOID *ppv) { if (ppv) *ppv = this; ++m_ref; return S_OK; }
    STDMETHOD_(ULONG,AddRef)() { return (ULONG) ++m_ref; }
    STDMETHOD_(ULONG,Release)() { const LONG r = --m_ref; if (r <= 0) { delete this; return 0; } return (ULONG) r; }

    STDMETHOD(CreateSoundBuffer)(LPCDSBUFFERDESC pDesc, LPDIRECTSOUNDBUFFER *ppBuf, LPUNKNOWN)
    {
        if (!pDesc || !ppBuf) return DSERR_INVALIDPARAM;
        *ppBuf = NULL;

        WAVEFORMATEX fmt;
        memset ( &fmt, 0, sizeof(fmt) );
        if (pDesc->lpwfxFormat) fmt = *pDesc->lpwfxFormat;
        else {
            //  The primary buffer is described without a format. It is never
            //  mixed - it exists so the caller can set the output format - so a
            //  sane one keeps its arithmetic valid.
            fmt.wFormatTag = WAVE_FORMAT_PCM;
            fmt.nChannels = 2; fmt.nSamplesPerSec = RANAUDIO_RATE;
            fmt.wBitsPerSample = 16; fmt.nBlockAlign = 4;
            fmt.nAvgBytesPerSec = RANAUDIO_RATE * 4;
        }

        const unsigned bytes = pDesc->dwBufferBytes;
        int voice = 0;
        if (bytes && !( pDesc->dwFlags & DSBCAPS_PRIMARYBUFFER ))
            voice = RanAudio_RingCreate ( bytes, fmt.nChannels, fmt.wBitsPerSample,
                                          fmt.nSamplesPerSec );

        LOGI ( "buffer: %u bytes, %d ch, %u Hz, %d bit, flags %08x -> voice %d",
               bytes, (int) fmt.nChannels, (unsigned) fmt.nSamplesPerSec,
               (int) fmt.wBitsPerSample, (unsigned) pDesc->dwFlags, voice );
        *ppBuf = new RanSoundBuffer ( voice, bytes, fmt, pDesc->dwFlags );
        return DS_OK;
    }

    STDMETHOD(GetCaps)(LPDSCAPS pCaps)
    {
        if (!pCaps) return DSERR_INVALIDPARAM;
        const DWORD size = pCaps->dwSize;
        memset ( pCaps, 0, size );
        pCaps->dwSize = size;
        pCaps->dwFlags = DSCAPS_CONTINUOUSRATE;
        pCaps->dwMinSecondarySampleRate = 8000;
        pCaps->dwMaxSecondarySampleRate = 48000;
        pCaps->dwFreeHwMixingAllBuffers = 32;
        return DS_OK;
    }

    STDMETHOD(DuplicateSoundBuffer)(LPDIRECTSOUNDBUFFER, LPDIRECTSOUNDBUFFER *ppDup)
    { if (ppDup) *ppDup = NULL; return E_NOTIMPL; }
    STDMETHOD(SetCooperativeLevel)(HWND, DWORD) { return DS_OK; }
    STDMETHOD(Compact)() { return DS_OK; }
    STDMETHOD(GetSpeakerConfig)(LPDWORD pCfg) { if (pCfg) *pCfg = DSSPEAKER_STEREO; return DS_OK; }
    STDMETHOD(SetSpeakerConfig)(DWORD) { return DS_OK; }
    STDMETHOD(Initialize)(LPCGUID) { return DS_OK; }
    STDMETHOD(VerifyCertification)(LPDWORD pCert) { if (pCert) *pCert = DS_CERTIFIED; return DS_OK; }
};

} // namespace

//  A buffer over a clip voice, for the sound-effect path: dsutil hands these
//  out so that SuperSound can SetVolume and SetPan on the instance it started.
extern "C" IDirectSoundBuffer *RanDSound_WrapClip ( int clip, unsigned bytes,
                                                    int channels, int bits, unsigned rate )
{
    const int voice = RanAudio_VoiceCreate ( clip );
    if (!voice) return NULL;

    WAVEFORMATEX fmt;
    memset ( &fmt, 0, sizeof(fmt) );
    fmt.wFormatTag      = WAVE_FORMAT_PCM;
    fmt.nChannels       = (WORD) ( channels > 0 ? channels : 2 );
    fmt.nSamplesPerSec  = rate ? rate : RANAUDIO_RATE;
    fmt.wBitsPerSample  = (WORD) ( bits > 0 ? bits : 16 );
    fmt.nBlockAlign     = (WORD) ( fmt.nChannels * fmt.wBitsPerSample / 8 );
    fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;

    return new RanSoundBuffer ( voice, bytes, fmt, 0 );
}

extern "C" HRESULT WINAPI RanDSound_Create ( LPDIRECTSOUND8 *ppDS8 )
{
    if (!ppDS8) return DSERR_INVALIDPARAM;
    *ppDS8 = new RanDirectSound();
    return DS_OK;
}
