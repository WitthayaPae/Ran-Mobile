// Desktop-shell pieces that have no mobile equivalent.
//
// Three Windows-only subsystems are referenced from live client code and must
// resolve, but none of them can be ported as-is:
//
//   DXInputString / CIMEEdit  — the Win32 IME text-entry control. Phase 4 wires
//                               the Android soft keyboard into the SAME API, so
//                               callers (chat, login fields) never change.
//   CCommonWeb                — the embedded Internet Explorer control used for
//                               the help, item-shop and web-link windows. Dead
//                               on mobile; the windows simply stay empty.
//   DirectInput / DirectSound entry points — replaced in phase 4 by touch input
//                               and OpenSL; the GUIDs must exist to link.
//
// Everything here reports "nothing happened" honestly rather than faking
// success in a way that would make a caller act on invented data.

#include "stdafx.h"
#include "DXInputString.h"
#include "IMEEdit.h"
#include "CommonWeb.h"

#include <android/log.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "RanShell", __VA_ARGS__)

// ------------------------------------------------------- CIMEEdit (text entry)
// Phase 4 replaces the body of these with calls into the Android IME. The
// buffer below already behaves like a real edit control so that the callers'
// caret/insert logic is exercised meanwhile.
static std::string g_imeText;
static int         g_imeCaret = 0;

const char *CIMEEdit::GetString() { return g_imeText.c_str(); }

void CIMEEdit::SetString(const char *szChange) {
    g_imeText = szChange ? szChange : "";
    g_imeCaret = (int)g_imeText.size();
}

int CIMEEdit::SetInsertPos(int xPos) {
    if (xPos < 0) xPos = 0;
    if (xPos > (int)g_imeText.size()) xPos = (int)g_imeText.size();
    g_imeCaret = xPos;
    return g_imeCaret;
}

bool CIMEEdit::CheckEnterKeyDown() { return false; }
BOOL CIMEEdit::IsNativeMode() { return FALSE; }
void CIMEEdit::SetIMEMode(HWND, DWORD, DWORD, BOOL) {}

// --------------------------------------------------- DXInputString (singleton)
BOOL DXInputString::m_bCaratMove = FALSE;

DXInputString::DXInputString() : m_bOn(FALSE), m_pParentWnd(NULL) {}

DXInputString &DXInputString::GetInstance() {
    static DXInputString s;
    return s;
}

void DXInputString::Create(CWnd *, const CRect &) {}
void DXInputString::Move(const CRect &) {}

BOOL DXInputString::OnInput() {
    m_bOn = TRUE;
    // Phase 4: show the Android soft keyboard here.
    return TRUE;
}
BOOL DXInputString::OffInput() {
    m_bOn = FALSE;
    // Phase 4: hide the soft keyboard here.
    return TRUE;
}
BOOL DXInputString::IsOn() { return m_bOn; }

// ---------------------------------------------------------------- CCommonWeb
// The in-game browser windows. Reporting "not created" makes every caller take
// its own already-existing "no web view" path instead of drawing an empty frame.
CCommonWeb *CCommonWeb::m_cpCommonWeb = NULL;

CCommonWeb::CCommonWeb() : m_pWnd(NULL), m_pBound(NULL), m_pVisible(NULL) {
    for (int i = 0; i < TOTAL_ID; ++i) { m_bCreate[i] = FALSE; m_bVisible[i] = FALSE; }
}
CCommonWeb::~CCommonWeb() {}

CCommonWeb *CCommonWeb::Get() {
    if (!m_cpCommonWeb) m_cpCommonWeb = new CCommonWeb();
    return m_cpCommonWeb;
}
VOID CCommonWeb::CleanUp() { delete m_cpCommonWeb; m_cpCommonWeb = NULL; }

VOID CCommonWeb::Create(CWnd *, BOOL *, RECT *) { LOGI("web views disabled on mobile"); }
VOID CCommonWeb::Navigate(INT, const TCHAR *, BOOL) {}
VOID CCommonWeb::Move(INT, INT, INT, INT, INT, BOOL, BOOL) {}
void CCommonWeb::Refresh(INT) {}
BOOL CCommonWeb::GetCreate(INT) { return FALSE; }
VOID CCommonWeb::SetVisible(INT, BOOL) {}
VOID CCommonWeb::SetVisible(BOOL) {}
BOOL CCommonWeb::GetVisible(INT) { return FALSE; }
bool CCommonWeb::IsCompleteLoad(INT) { return false; }
void CCommonWeb::ReSetCompleteLoad(INT) {}

// ------------------------------------------- DirectInput / DirectSound entries
extern "C" {

// DirectInput8Create lives in dinput_mobile.cpp, where a real device is fed
// from Android touch and key events.
const GUID IID_IDirectInput8A =
    { 0xbf798030, 0x483a, 0x4da2, { 0xaa, 0x99, 0x5d, 0x64, 0xed, 0x36, 0x97, 0x00 } };
const GUID GUID_SysKeyboard =
    { 0x6f1d2b61, 0xd5a0, 0x11cf, { 0xbf, 0xc7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } };
const GUID GUID_SysMouse =
    { 0x6f1d2b60, 0xd5a0, 0x11cf, { 0xbf, 0xc7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } };

// The device-format descriptors DirectInput would hand back. Zeroed: no device
// is ever created, so nothing reads past the header.
const DIDATAFORMAT c_dfDIKeyboard = { sizeof(DIDATAFORMAT), sizeof(DIOBJECTDATAFORMAT), 0x2, 256, 0, NULL };
const DIDATAFORMAT c_dfDIMouse2   = { sizeof(DIDATAFORMAT), sizeof(DIOBJECTDATAFORMAT), 0x2, 20,  0, NULL };

// DirectSound: the wrapper in dsutil_mobile.cpp never calls this (it goes
// straight to the silent backend), but BgmSound references it directly.
HRESULT WINAPI DirectSoundCreate8(const GUID *, LPDIRECTSOUND8 *ppDS8, LPUNKNOWN) {
    if (ppDS8) *ppDS8 = NULL;
    return E_NOTIMPL;
}
const GUID DS3DALG_HRTF_FULL =
    { 0xc2f5f0aa, 0xd2f0, 0x11d2, { 0x8e, 0xd9, 0x00, 0x60, 0x97, 0x11, 0x00, 0x00 } };
const GUID DS3DALG_HRTF_LIGHT =
    { 0xc2f5f0ab, 0xd2f0, 0x11d2, { 0x8e, 0xd9, 0x00, 0x60, 0x97, 0x11, 0x00, 0x00 } };

} // extern "C"

// ---------------------------------------------------- misc desktop leftovers
// Two flag helpers from Lib_Helper/EtcFunction.cpp. That file is MFC dialog
// code and is not built for mobile, but these two are pure bit twiddling and
// live client code calls them.
void SetCheck_Flags(BOOL bCheck, DWORD &dwFlags, DWORD dwOnOffFlag) {
    if (bCheck) dwFlags |= dwOnOffFlag;
    else        dwFlags &= ~dwOnOffFlag;
}
bool GetCheck_Flags(DWORD &dwFlags, DWORD dwOnOffFlag) {
    return (dwFlags & dwOnOffFlag) != 0;
}

CIMEEdit::CIMEEdit() {}
LRESULT CIMEEdit::WindowProc(UINT, WPARAM, LPARAM) { return 0; }
CIMEEdit::~CIMEEdit() {}

extern "C" {
// Intel JPEG Library — used only by the screenshot/bmp2jpeg path. Phase 3
// swaps in stb_image_write; until then screenshots simply fail.
int __stdcall ijlInit(void *) { return -1; }
int __stdcall ijlFree(void *) { return 0; }

// D3DXCreateFontIndirectA now lives in shim/d3d/d3dx_font.cpp, where it is real.
}
