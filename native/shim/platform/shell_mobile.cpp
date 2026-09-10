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
#include "ran_plat.h"
#include "DXInputString.h"
#include "IMEEdit.h"
#include "CommonWeb.h"


#define LOGI(...) RanPlat_Log(RANLOG_INFO, "RanShell", __VA_ARGS__)

// ------------------------------------------------------- CIMEEdit (text entry)
// Phase 4 replaces the body of these with calls into the Android IME. The
// buffer below already behaves like a real edit control so that the callers'
// caret/insert logic is exercised meanwhile.
static std::string g_imeText;

//  The caret lives in CIMEEdit::m_xCaretPos, not in a static here.
//
//  It used to be a static, which nothing outside this file could see - and
//  GetInsertPos() is an inline in IMEEdit.h that returns m_xCaretPos, so the
//  client read a caret the shim never wrote. It was always 0. CUIEditBox reads
//  it every frame into m_xInsertPos and pushes it back through SetInsertPos, so
//  the 0 was made true: focusing a field that already had text pinned the caret
//  to the front, where backspace correctly has nothing to delete and typing
//  prepends. Keeping the caret in the member the getter exposes makes the two
//  agree.
//
//  The free functions below have no 'this', so they reach it through whichever
//  CIMEEdit last touched the buffer - there is only ever the one that
//  DXInputString owns - using the public accessors.
static CIMEEdit *g_imeEdit = NULL;

static int imeCaret() { return g_imeEdit ? g_imeEdit->GetInsertPos() : 0; }
static void imeSetCaret(int c) { if (g_imeEdit) g_imeEdit->SetInsertPos(c); }

const char *CIMEEdit::GetString() { g_imeEdit = this; return g_imeText.c_str(); }

void CIMEEdit::SetString(const char *szChange) {
    g_imeEdit = this;
    g_imeText = szChange ? szChange : "";
    m_xCaretPos = (int)g_imeText.size();
}

int CIMEEdit::SetInsertPos(int xPos) {
    g_imeEdit = this;
    if (xPos < 0) xPos = 0;
    if (xPos > (int)g_imeText.size()) xPos = (int)g_imeText.size();
    m_xCaretPos = xPos;
    return m_xCaretPos;
}

//  Put the caret at the end of whatever the field already holds.
//
//  BeginEdit seeds the buffer and then calls CUIEditBox::SetInsertPos(), which
//  passes the box's own m_xInsertPos - 0 on first focus. On a desktop that is
//  harmless because clicking into the text positions the caret; with a finger
//  and a soft keyboard there is no such gesture, so the caret has to start
//  somewhere useful, and the end is where a keyboard user expects it.
extern "C" void RanIME_CaretToEnd(void) { imeSetCaret((int)g_imeText.size()); }

//  Typed characters arrive here.
//
//  Until now nothing could put text in this buffer: SetString is only ever
//  called by the client to seed a field, so the edit boxes were unreachable from
//  the keyboard and typing did nothing at all. The on-screen keyboard the client
//  drew was the only way in, which is why removing it made login impossible.
//
//  The caret is a byte offset, so stepping over a character means stepping over
//  a whole UTF-8 sequence, not one byte.
//  UTF-8 in, CP874 out.
//
//  Everything else in this buffer is CP874: SetString seeds it from the client,
//  whose own strings come from Gui.rcc and the item tables, and every place the
//  client compares typed text against its own data does it byte for byte -
//  CGMGenItemWindow::ApplyFilter is a plain CString::Find. A Windows Thai IME
//  hands the client CP874, so the client has never had to care.
//
//  Appending UTF-8 made the buffer two encodings at once. ASCII still worked,
//  because the two agree there, which is why login and English search were fine
//  and Thai matched nothing at all - "ดาบ" typed is E0 B8 94 E0 B8 B2 E0 B8 9A
//  and the same word in the item table is B4 D2 BA. Nothing could ever match.
//
//  0x00-0x7F is ASCII and 0xA1-0xFB maps linearly onto the Thai block at
//  U+0E01 - the inverse of cp874() in gdi_text.cpp, which is what draws it.
static void appendCp874(std::string &out, unsigned cp) {
    if (cp < 0x80) { out += (char)cp; return; }
    if (cp >= 0x0E01 && cp <= 0x0E5B) { out += (char)(0xA0 + (cp - 0x0E00)); return; }
    switch (cp) {
        case 0x20AC: out += (char)0x80; return;   //  euro
        case 0x00A0: out += (char)0xA0; return;   //  no-break space
        case 0x2026: out += (char)0x85; return;
        case 0x2018: out += (char)0x91; return;
        case 0x2019: out += (char)0x92; return;
        case 0x201C: out += (char)0x93; return;
        case 0x201D: out += (char)0x94; return;
        case 0x2022: out += (char)0x95; return;
        case 0x2013: out += (char)0x96; return;
        case 0x2014: out += (char)0x97; return;
        //  Anything the codepage cannot hold is dropped rather than written as
        //  a replacement byte: a stray 0x3F in a name is worse than a shorter
        //  string, because it matches nothing and looks like a typo.
        default: return;
    }
}

extern "C" void RanIME_InsertUtf8(const char *sz) {
    if (!sz || !*sz) return;

    std::string cooked;
    for (const unsigned char *p = (const unsigned char *)sz; *p; ) {
        unsigned cp = 0;
        int n = 0;
        if      (*p < 0x80)        { cp = *p;          n = 1; }
        else if ((*p & 0xE0) == 0xC0) { cp = *p & 0x1F; n = 2; }
        else if ((*p & 0xF0) == 0xE0) { cp = *p & 0x0F; n = 3; }
        else if ((*p & 0xF8) == 0xF0) { cp = *p & 0x07; n = 4; }
        else { ++p; continue; }                 //  stray continuation byte

        int i = 1;
        for (; i < n; ++i) {
            if ((p[i] & 0xC0) != 0x80) break;   //  truncated - give up on it
            cp = (cp << 6) | (p[i] & 0x3F);
        }
        if (i < n) { ++p; continue; }
        appendCp874(cooked, cp);
        p += n;
    }
    if (cooked.empty()) return;

    int c = imeCaret();
    if (c < 0) c = 0;
    if (c > (int)g_imeText.size()) c = (int)g_imeText.size();
    g_imeText.insert((size_t)c, cooked);
    imeSetCaret(c + (int)cooked.size());
}

extern "C" void RanIME_Backspace(void) {
    const int c = imeCaret();
    if (c <= 0 || g_imeText.empty()) return;
    //  One byte. The buffer is CP874 now, where every character is one byte -
    //  stepping back over UTF-8 continuation bytes would eat a Thai character
    //  and one or two of its neighbours, because CP874 Thai lives in the same
    //  0x80-0xBF range that marks a continuation byte in UTF-8.
    g_imeText.erase((size_t)(c - 1), 1);
    imeSetCaret(c - 1);
}

extern "C" void RanIME_ClearText(void) { g_imeText.clear(); imeSetCaret(0); }

extern "C" int RanInput_TakeEnter(void);

//  Was Return pressed in this field since anyone last asked?
//
//  Returning a flat false meant chat was never sent: BasicChatRightBody wants
//  DIK_RETURN down AND this, and only the first of those was ever true.
bool CIMEEdit::CheckEnterKeyDown() { return RanInput_TakeEnter() != 0; }
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

// DirectSound. BgmSound calls this directly and then drives the buffer itself -
// Lock, Unlock, GetCurrentPosition - so what comes back has to be a real ring,
// which is what dsound_mobile.cpp builds on top of the mixer.
HRESULT WINAPI RanDSound_Create(LPDIRECTSOUND8 *ppDS8);
HRESULT WINAPI DirectSoundCreate8(const GUID *, LPDIRECTSOUND8 *ppDS8, LPUNKNOWN) {
    return RanDSound_Create(ppDS8);
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
