#pragma once
#include "windows.h"
typedef UINT MMRESULT;
#define MMSYSERR_NOERROR 0
#define WAVE_FORMAT_PCM 1
typedef struct tagWAVEFORMATEX {
    WORD wFormatTag, nChannels; DWORD nSamplesPerSec, nAvgBytesPerSec;
    WORD nBlockAlign, wBitsPerSample, cbSize;
} WAVEFORMATEX, *LPWAVEFORMATEX;

typedef HANDLE HMMIO;
typedef struct _MMCKINFO { DWORD ckid, cksize, fccType, dwDataOffset, dwFlags; } MMCKINFO, *LPMMCKINFO;
typedef struct _MMIOINFO { DWORD dwFlags, fccIOProc; void *pIOProc; UINT wErrorRet;
    HTASK htask; LONG cchBuffer; char *pchBuffer, *pchNext, *pchEndRead, *pchEndWrite;
    LONG lBufOffset, lDiskOffset; DWORD adwInfo[3], dwReserved1, dwReserved2; HMMIO hmmio; } MMIOINFO, *LPMMIOINFO;
typedef DWORD FOURCC;
#define MMIO_READ 0x0000
#define MMIO_FINDCHUNK 0x0010
#define MMIO_FINDRIFF 0x0020
#ifdef __cplusplus
inline MMRESULT timeKillEvent(UINT) { return 0; }
inline MMRESULT timeSetEvent(UINT, UINT, void *, DWORD_PTR, UINT) { return 0; }
inline HMMIO mmioOpen(LPSTR, LPMMIOINFO, DWORD) { return NULL; }
inline MMRESULT mmioClose(HMMIO, UINT) { return 0; }
inline LONG mmioRead(HMMIO, char *, LONG) { return 0; }
inline LONG mmioSeek(HMMIO, LONG, int) { return 0; }
inline MMRESULT mmioDescend(HMMIO, LPMMIOINFO, const void *, UINT) { return 1; }
inline MMRESULT mmioAscend(HMMIO, LPMMIOINFO, UINT) { return 0; }
#endif
#define mmioFOURCC(a,b,c,d) ((DWORD)(BYTE)(a)|((DWORD)(BYTE)(b)<<8)|((DWORD)(BYTE)(c)<<16)|((DWORD)(BYTE)(d)<<24))
#define FOURCC_RIFF mmioFOURCC('R','I','F','F')
#define FOURCC_LIST mmioFOURCC('L','I','S','T')
#ifdef __cplusplus
inline MMRESULT mmioGetInfo(HMMIO, LPMMIOINFO, UINT) { return 1; }
inline MMRESULT mmioSetInfo(HMMIO, LPMMIOINFO, UINT) { return 0; }
inline MMRESULT mmioAdvance(HMMIO, LPMMIOINFO, UINT) { return 1; }
inline MMRESULT mmioCreateChunk(HMMIO, LPMMIOINFO, UINT) { return 1; }
#endif
typedef void (CALLBACK *LPTIMECALLBACK)(UINT, UINT, DWORD_PTR, DWORD_PTR, DWORD_PTR);
