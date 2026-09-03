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
        const unsigned at = m_voice ? RanAudio_VoicePosition ( m_voice ) : 0;
        if (pPlay)  *pPlay = at;
        //  The write cursor is where it is no longer safe to write. Ahead of
        //  the play cursor by one mixer buffer, which is what the hardware
        //  would report and what the streaming code leaves room for.
        if (pWrite) *pWrite = m_bytes ? ( at + m_fmt.nBlockAlign * 1024 ) % m_bytes : 0;
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
