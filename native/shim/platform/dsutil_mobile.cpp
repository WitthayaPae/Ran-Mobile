// DirectSound wrapper (dsutil) — mobile implementation.
//
// PHASE 2 STATUS: silent. Every object is created and every call succeeds, so
// the engine's whole sound-management layer above this (DxSoundMan, SSound,
// BgmSound, StaticSoundMan, MovableSound) runs for real — it loads its sets,
// tracks 3D positions, starts and stops sources — with nothing reaching a
// speaker. That keeps the sound *logic* exercised during a headless boot
// instead of stubbing it out wholesale.
//
// PHASE 4 replaces the bodies with OpenSL ES / AAudio. The class shapes here
// are dsutil.h's, unchanged, so that swap touches this file only.

#include "stdafx.h"
#include "ran_plat.h"
#include "dsutil.h"

#include <string.h>
#include <set>
#include <string>

#define LOGI(...) RanPlat_Log(RANLOG_INFO, "RanSound", __VA_ARGS__)

namespace {
std::set<std::string> g_loaded;   // distinct wave files the boot asks for
unsigned g_plays = 0;
}

// Reported by the platform layer at shutdown: how much audio the run wanted.
extern "C" void RanSound_LogStats(void) {
    LOGI("sound — %zu distinct wave files requested, %u play calls (silent: phase 4)",
         g_loaded.size(), g_plays);
}

// ------------------------------------------------------------- CSoundManager
CSoundManager::CSoundManager() : m_pDS(NULL), m_DlgID(0) {}
CSoundManager::~CSoundManager() {}

HRESULT CSoundManager::EnumDevice(HWND, int) { return S_OK; }

HRESULT CSoundManager::Initialize(HWND, DWORD, DWORD channels, DWORD freq, DWORD bits) {
    LOGI("sound init — %lu ch, %lu Hz, %lu bit (silent backend)",
         (unsigned long)channels, (unsigned long)freq, (unsigned long)bits);
    return S_OK;
}

HRESULT CSoundManager::SetPrimaryBufferFormat(DWORD, DWORD, DWORD) { return S_OK; }

HRESULT CSoundManager::Get3DListenerInterface(LPDIRECTSOUND3DLISTENER *ppDSListener) {
    if (ppDSListener) *ppDSListener = NULL;
    return E_FAIL;      // callers treat a missing listener as "no 3D", not an error
}

HRESULT CSoundManager::Create(CSound **ppSound, LPTSTR strWaveFileName, DWORD, GUID, DWORD numBuffers) {
    if (!ppSound) return E_INVALIDARG;
    if (strWaveFileName) g_loaded.insert(strWaveFileName);
    *ppSound = new CSound(NULL, 0, numBuffers ? numBuffers : 1, NULL);
    return S_OK;
}

HRESULT CSoundManager::CreateFromMemory(CSound **ppSound, BYTE *, ULONG, LPWAVEFORMATEX,
                                        DWORD, GUID, DWORD numBuffers) {
    if (!ppSound) return E_INVALIDARG;
    *ppSound = new CSound(NULL, 0, numBuffers ? numBuffers : 1, NULL);
    return S_OK;
}

HRESULT CSoundManager::CreateStreaming(CStreamingSound **ppStreamingSound, LPTSTR strWaveFileName,
                                       DWORD, GUID, DWORD, DWORD notifySize, HANDLE) {
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

CSound::~CSound() {}

HRESULT CSound::RestoreBuffer(LPDIRECTSOUNDBUFFER, BOOL *pbWasRestored) {
    if (pbWasRestored) *pbWasRestored = FALSE;
    return S_OK;
}
HRESULT CSound::Get3DBufferInterface(DWORD, LPDIRECTSOUND3DBUFFER *ppDS3DBuffer) {
    if (ppDS3DBuffer) *ppDS3DBuffer = NULL;
    return E_FAIL;
}
HRESULT CSound::FillBufferWithSound(LPDIRECTSOUNDBUFFER, BOOL) { return S_OK; }
LPDIRECTSOUNDBUFFER CSound::GetFreeBuffer() { return NULL; }
LPDIRECTSOUNDBUFFER CSound::GetBuffer(DWORD) { return NULL; }
HRESULT CSound::Play(DWORD, DWORD) { ++g_plays; return S_OK; }
HRESULT CSound::Stop() { return S_OK; }
HRESULT CSound::Reset() { return S_OK; }
BOOL    CSound::IsSoundPlaying() { return FALSE; }   // silent: nothing is ever playing
LPDIRECTSOUNDBUFFER CSound::GetFreeBuffer(DWORD &BufferID) { BufferID = 0; return NULL; }
HRESULT CSound::PlayBuffer(DWORD &BufferID, DWORD, DWORD) { BufferID = 0; ++g_plays; return S_OK; }
HRESULT CSound::StopBuffer(DWORD) { return S_OK; }
HRESULT CSound::ResetBuffer(DWORD) { return S_OK; }
BOOL    CSound::IsSoundPlayingBuffer(DWORD) { return FALSE; }

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
// Kept as a real (if empty) object: callers query format and size and would
// otherwise dereference null.
CWaveFile::CWaveFile() {
    m_pwfx = NULL; m_hmmio = NULL; m_pResourceBuffer = NULL;
    m_dwSize = 0; m_bIsReadingFromMemory = FALSE;
    memset(&m_ck, 0, sizeof(m_ck));
    memset(&m_ckRiff, 0, sizeof(m_ckRiff));
    memset(&m_mmioinfoOut, 0, sizeof(m_mmioinfoOut));
}
CWaveFile::~CWaveFile() { Close(); }

HRESULT CWaveFile::Open(LPTSTR strFileName, WAVEFORMATEX *pwfx, DWORD) {
    (void)strFileName; (void)pwfx;
    // A silent, valid 16-bit stereo 44.1 kHz format keeps every caller's maths sane.
    if (!m_pwfx) {
        m_pwfx = new WAVEFORMATEX;
        memset(m_pwfx, 0, sizeof(*m_pwfx));
        m_pwfx->wFormatTag = WAVE_FORMAT_PCM;
        m_pwfx->nChannels = 2;
        m_pwfx->nSamplesPerSec = 44100;
        m_pwfx->wBitsPerSample = 16;
        m_pwfx->nBlockAlign = 4;
        m_pwfx->nAvgBytesPerSec = 44100 * 4;
    }
    m_dwSize = 0;
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
    if (pBuffer && dwSizeToRead) memset(pBuffer, 0, dwSizeToRead);   // silence
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
