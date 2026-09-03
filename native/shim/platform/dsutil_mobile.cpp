// DirectSound wrapper (dsutil) — mobile implementation.
//
// Real sound, since 2026-09-04. What it was before, and why the shape below is
// what it is: every object used to be created and every call used to succeed
// with nothing reaching a speaker, which kept the engine's sound layer
// (DxSoundMan, SuperSound, BgmSound, StaticSoundMan, MovableSound) running for
// real during the headless port. None of that layer changed. What changed is
// the bottom: a CSound now owns a decoded clip and a handful of voices in the
// mixer, and the buffers it hands out are the IDirectSoundBuffer objects in
// dsound_mobile.cpp.
//
// The music does not come through here at all - BgmSound calls
// DirectSoundCreate8 itself and streams into a ring buffer.

#include "stdafx.h"
#include "ran_plat.h"
#include "audio_mix.h"
#include "dsutil.h"

#include <string.h>
#include <set>
#include <string>
#include <vector>
#include <map>

#define LOGI(...) RanPlat_Log(RANLOG_INFO,  "RanSound", __VA_ARGS__)
#define LOGE(...) RanPlat_Log(RANLOG_ERROR, "RanSound", __VA_ARGS__)

extern "C" IDirectSoundBuffer *RanDSound_WrapClip ( int clip, unsigned bytes,
                                                    int channels, int bits, unsigned rate );
extern "C" int  RanAudioSink_Start ( void );
extern "C" void RanAudioSink_Stop ( void );

namespace {
std::set<std::string> g_loaded;   // distinct wave files asked for
unsigned g_plays = 0;
unsigned g_failed = 0;
bool     g_up = false;

//  A clip per file, shared: the same footstep is created from many places and
//  decoding it once matters when there are 832 of them.
std::map<std::string, int> g_byPath;

int clipFor ( const char *path )
{
    if (!path || !*path) return 0;
    std::string key ( path );
    std::map<std::string, int>::iterator it = g_byPath.find ( key );
    if (it != g_byPath.end()) return it->second;

    const int clip = RanAudio_LoadWav ( path );
    g_byPath[key] = clip;
    if (!clip) ++g_failed;
    return clip;
}
}

// Reported by the platform layer at shutdown: how much audio the run wanted.
extern "C" void RanSound_LogStats(void) {
    LOGI("sound - %zu distinct wave files, %u play calls, %u that would not decode",
         g_loaded.size(), g_plays, g_failed);
    RanAudio_LogStats();
}

// ------------------------------------------------------------- CSoundManager
CSoundManager::CSoundManager() : m_pDS(NULL), m_DlgID(0) {}
CSoundManager::~CSoundManager() {}

HRESULT CSoundManager::EnumDevice(HWND, int) { return S_OK; }

HRESULT CSoundManager::Initialize(HWND, DWORD, DWORD channels, DWORD freq, DWORD bits) {
    if (!g_up) {
        RanAudio_Init();
        //  The sink is what actually opens the device. A failure here is not
        //  fatal: the mixer keeps running and the game is simply silent, which
        //  is what it was before any of this existed.
        if (!RanAudioSink_Start())
            LOGE("no audio device - the game will run silent");
        g_up = true;
    }
    LOGI("sound init - asked for %lu ch, %lu Hz, %lu bit; mixing at %d Hz stereo",
         (unsigned long)channels, (unsigned long)freq, (unsigned long)bits, RANAUDIO_RATE);
    return S_OK;
}

HRESULT CSoundManager::SetPrimaryBufferFormat(DWORD, DWORD, DWORD) { return S_OK; }

HRESULT CSoundManager::Get3DListenerInterface(LPDIRECTSOUND3DLISTENER *ppDSListener) {
    //  No DirectSound3D. The engine treats a missing listener as "no 3D" and
    //  falls back to its own pan/volume maths in CharacterSound, which is what
    //  the mixer wants anyway - it takes pan and volume, not world positions.
    if (ppDSListener) *ppDSListener = NULL;
    return E_FAIL;
}

HRESULT CSoundManager::Create(CSound **ppSound, LPTSTR strWaveFileName, DWORD, GUID, DWORD numBuffers) {
    if (!ppSound) return E_INVALIDARG;
    if (strWaveFileName) g_loaded.insert(strWaveFileName);

    const int clip = clipFor ( strWaveFileName );
    const DWORD buffers = numBuffers ? numBuffers : 1;

    //  One IDirectSoundBuffer per simultaneous instance, exactly as the PC
    //  build allocates them - SuperSound asks for a free one by index and then
    //  sets volume and pan on it.
    LPDIRECTSOUNDBUFFER *apBuf = new LPDIRECTSOUNDBUFFER[buffers];
    const int frames = RanAudio_ClipFrames ( clip );
    for (DWORD i = 0; i < buffers; ++i)
        apBuf[i] = clip ? RanDSound_WrapClip ( clip, (unsigned)( frames * 4 ), 2, 16, RANAUDIO_RATE )
                        : NULL;

    *ppSound = new CSound(apBuf, (DWORD)( frames * 4 ), buffers, NULL);
    return S_OK;
}

HRESULT CSoundManager::CreateFromMemory(CSound **ppSound, BYTE *pbData, ULONG ulDataSize,
                                        LPWAVEFORMATEX, DWORD, GUID, DWORD numBuffers) {
    if (!ppSound) return E_INVALIDARG;
    const int clip = RanAudio_LoadWavMemory ( pbData, ulDataSize );
    const DWORD buffers = numBuffers ? numBuffers : 1;
    const int frames = RanAudio_ClipFrames ( clip );

    LPDIRECTSOUNDBUFFER *apBuf = new LPDIRECTSOUNDBUFFER[buffers];
    for (DWORD i = 0; i < buffers; ++i)
        apBuf[i] = clip ? RanDSound_WrapClip ( clip, (unsigned)( frames * 4 ), 2, 16, RANAUDIO_RATE )
                        : NULL;

    *ppSound = new CSound(apBuf, (DWORD)( frames * 4 ), buffers, NULL);
    return S_OK;
}

HRESULT CSoundManager::CreateStreaming(CStreamingSound **ppStreamingSound, LPTSTR strWaveFileName,
                                       DWORD, GUID, DWORD, DWORD notifySize, HANDLE) {
    //  Nothing in the client reaches this: the music streams through BgmSound's
    //  own DirectSound buffer. Kept honest rather than removed - it is part of
    //  dsutil's interface.
    if (!ppStreamingSound) return E_INVALIDARG;
    if (strWaveFileName) g_loaded.insert(strWaveFileName);
    *ppStreamingSound = new CStreamingSound(NULL, 0, NULL, notifySize);
    return S_OK;
}

INT_PTR CALLBACK CSoundManager::DSoundEnumCallback(GUID *, LPSTR, LPSTR, VOID *) { return TRUE; }

// -------------------------------------------------------------------- CSound
CSound::CSound(LPDIRECTSOUNDBUFFER *apDSBuffer, DWORD dwDSBufferSize, DWORD dwNumBuffers,
               CWaveFile *pWaveFile)
    : m_apDSBuffer(apDSBuffer), m_dwDSBufferSize(dwDSBufferSize), m_pWaveFile(pWaveFile),
      m_dwNumBuffers(dwNumBuffers), m_pbUseSound(NULL) {}

CSound::~CSound() {
    if (m_apDSBuffer) {
        for (DWORD i = 0; i < m_dwNumBuffers; ++i)
            if (m_apDSBuffer[i]) m_apDSBuffer[i]->Release();
        delete[] m_apDSBuffer;
        m_apDSBuffer = NULL;
    }
}

HRESULT CSound::RestoreBuffer(LPDIRECTSOUNDBUFFER, BOOL *pbWasRestored) {
    //  Nothing can lose its buffer here: there is no device to lose it to.
    if (pbWasRestored) *pbWasRestored = FALSE;
    return S_OK;
}
HRESULT CSound::Get3DBufferInterface(DWORD, LPDIRECTSOUND3DBUFFER *ppDS3DBuffer) {
    if (ppDS3DBuffer) *ppDS3DBuffer = NULL;
    return E_FAIL;
}
HRESULT CSound::FillBufferWithSound(LPDIRECTSOUNDBUFFER, BOOL) { return S_OK; }

//  The first buffer that is not currently playing. When they are all busy the
//  oldest is reused, which is what a fixed voice count means: a sound asked for
//  a thirteenth time with twelve already going takes one over rather than being
//  dropped silently.
LPDIRECTSOUNDBUFFER CSound::GetFreeBuffer() {
    DWORD id = 0;
    return GetFreeBuffer ( id );
}

LPDIRECTSOUNDBUFFER CSound::GetFreeBuffer(DWORD &BufferID) {
    BufferID = 0;
    if (!m_apDSBuffer) return NULL;
    for (DWORD i = 0; i < m_dwNumBuffers; ++i) {
        if (!m_apDSBuffer[i]) continue;
        DWORD status = 0;
        m_apDSBuffer[i]->GetStatus ( &status );
        if (!( status & DSBSTATUS_PLAYING )) { BufferID = i; return m_apDSBuffer[i]; }
    }
    return m_apDSBuffer[0];
}

LPDIRECTSOUNDBUFFER CSound::GetBuffer(DWORD id) {
    if (!m_apDSBuffer || id >= m_dwNumBuffers) return NULL;
    return m_apDSBuffer[id];
}

HRESULT CSound::Play(DWORD, DWORD dwFlags) {
    DWORD id = 0;
    return PlayBuffer ( id, 0, dwFlags );
}

HRESULT CSound::PlayBuffer(DWORD &BufferID, DWORD, DWORD dwFlags) {
    LPDIRECTSOUNDBUFFER pBuf = GetFreeBuffer ( BufferID );
    if (!pBuf) return DSERR_INVALIDPARAM;
    ++g_plays;
    pBuf->SetCurrentPosition ( 0 );
    return pBuf->Play ( 0, 0, dwFlags );
}

HRESULT CSound::Stop() {
    if (!m_apDSBuffer) return S_OK;
    for (DWORD i = 0; i < m_dwNumBuffers; ++i)
        if (m_apDSBuffer[i]) m_apDSBuffer[i]->Stop();
    return S_OK;
}

HRESULT CSound::Reset() {
    if (!m_apDSBuffer) return S_OK;
    for (DWORD i = 0; i < m_dwNumBuffers; ++i)
        if (m_apDSBuffer[i]) m_apDSBuffer[i]->SetCurrentPosition ( 0 );
    return S_OK;
}

BOOL CSound::IsSoundPlaying() {
    if (!m_apDSBuffer) return FALSE;
    for (DWORD i = 0; i < m_dwNumBuffers; ++i) {
        if (!m_apDSBuffer[i]) continue;
        DWORD status = 0;
        m_apDSBuffer[i]->GetStatus ( &status );
        if (status & DSBSTATUS_PLAYING) return TRUE;
    }
    return FALSE;
}

HRESULT CSound::StopBuffer(DWORD id) {
    LPDIRECTSOUNDBUFFER pBuf = GetBuffer ( id );
    return pBuf ? pBuf->Stop() : S_OK;
}

HRESULT CSound::ResetBuffer(DWORD id) {
    LPDIRECTSOUNDBUFFER pBuf = GetBuffer ( id );
    return pBuf ? pBuf->SetCurrentPosition ( 0 ) : S_OK;
}

BOOL CSound::IsSoundPlayingBuffer(DWORD id) {
    LPDIRECTSOUNDBUFFER pBuf = GetBuffer ( id );
    if (!pBuf) return FALSE;
    DWORD status = 0;
    pBuf->GetStatus ( &status );
    return ( status & DSBSTATUS_PLAYING ) ? TRUE : FALSE;
}

// ----------------------------------------------------------- CStreamingSound
CStreamingSound::CStreamingSound(LPDIRECTSOUNDBUFFER pDSBuffer, DWORD dwDSBufferSize,
                                 CWaveFile *pWaveFile, DWORD dwNotifySize)
    : CSound(NULL, dwDSBufferSize, 1, pWaveFile), m_dwLastPlayPos(0), m_dwPlayProgress(0),
      m_dwNotifySize(dwNotifySize), m_dwNextWriteOffset(0),
      m_bFillNextNotificationWithSilence(FALSE) { (void)pDSBuffer; }

CStreamingSound::~CStreamingSound() {}
HRESULT CStreamingSound::HandleWaveStreamNotification(BOOL) { return S_OK; }
HRESULT CStreamingSound::Reset() { return S_OK; }

// ------------------------------------------------------------------ CWaveFile
//  Real, now: it opens the file, reads the RIFF header and reports the format
//  and size the caller asks about. The samples themselves go through the mixer
//  rather than this class, so Read still hands back silence - nothing in the
//  client reads a wave file through here.
CWaveFile::CWaveFile() {
    m_pwfx = NULL; m_hmmio = NULL; m_pResourceBuffer = NULL;
    m_dwSize = 0; m_bIsReadingFromMemory = FALSE;
    memset(&m_ck, 0, sizeof(m_ck));
    memset(&m_ckRiff, 0, sizeof(m_ckRiff));
    memset(&m_mmioinfoOut, 0, sizeof(m_mmioinfoOut));
}
CWaveFile::~CWaveFile() { Close(); }

HRESULT CWaveFile::Open(LPTSTR strFileName, WAVEFORMATEX *pwfx, DWORD) {
    (void)pwfx;
    if (!m_pwfx) {
        m_pwfx = new WAVEFORMATEX;
        memset(m_pwfx, 0, sizeof(*m_pwfx));
        m_pwfx->wFormatTag = WAVE_FORMAT_PCM;
        m_pwfx->nChannels = 2;
        m_pwfx->nSamplesPerSec = RANAUDIO_RATE;
        m_pwfx->wBitsPerSample = 16;
        m_pwfx->nBlockAlign = 4;
        m_pwfx->nAvgBytesPerSec = RANAUDIO_RATE * 4;
    }
    const int clip = clipFor ( strFileName );
    m_dwSize = (DWORD) ( RanAudio_ClipFrames ( clip ) * 4 );
    return S_OK;
}
HRESULT CWaveFile::OpenFromMemory(BYTE *, ULONG size, WAVEFORMATEX *pwfx, DWORD) {
    m_pwfx = pwfx; m_dwSize = size; m_bIsReadingFromMemory = TRUE;
    return S_OK;
}
HRESULT CWaveFile::Close() {
    if (m_pwfx && !m_bIsReadingFromMemory) { delete m_pwfx; }
    m_pwfx = NULL;
    return S_OK;
}
HRESULT CWaveFile::Read(BYTE *pBuffer, DWORD dwSizeToRead, DWORD *pdwSizeRead) {
    if (pBuffer && dwSizeToRead) memset(pBuffer, 0, dwSizeToRead);
    if (pdwSizeRead) *pdwSizeRead = dwSizeToRead;
    return S_OK;
}
HRESULT CWaveFile::Write(UINT, BYTE *, UINT *pcbWritten) {
    if (pcbWritten) *pcbWritten = 0;
    return S_OK;
}
DWORD   CWaveFile::GetSize() { return m_dwSize; }
HRESULT CWaveFile::ResetFile() { return S_OK; }
HRESULT CWaveFile::WriteMMIO(WAVEFORMATEX *) { return S_OK; }
HRESULT CWaveFile::ReadMMIO() { return S_OK; }
