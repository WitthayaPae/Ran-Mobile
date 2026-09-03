// Minimal MFC replacement for the RAN native mobile port.
// Only the classes the client actually uses: CString, CTime, CRect/CPoint/CSize,
// CFile, collections, CCriticalSection, CFileFind, plus inert CWnd/CDC stubs.
#pragma once
#ifndef RAN_MFC_COMPAT_H
#define RAN_MFC_COMPAT_H

#include "fmt_msvc.h"
#include "windows.h"
#include <string>
#include <vector>
#include <list>
#include <map>
#include <algorithm>

#ifndef ASSERT
#define ASSERT(x)        ((void)0)
#define ASSERT_VALID(x)  ((void)0)
#define VERIFY(x)        ((void)(x))
#define TRACE(...)       ((void)0)
#define TRACE0(x)        ((void)0)
#define TRACE1(...)      ((void)0)
#define TRACE2(...)      ((void)0)
#define TRACE3(...)      ((void)0)
#define AFX_EXT_CLASS
#define AFXAPI
#define DECLARE_DYNAMIC(x)
#define IMPLEMENT_DYNAMIC(x,y)
#define DECLARE_DYNCREATE(x)
#define IMPLEMENT_DYNCREATE(x,y)
#define DECLARE_SERIAL(x)
#define IMPLEMENT_SERIAL(x,y,z)
#define DECLARE_MESSAGE_MAP()
#define BEGIN_MESSAGE_MAP(a,b) /* removed */
#define END_MESSAGE_MAP()
#define afx_msg
#endif

class CObject { public: virtual ~CObject() {} };
class CException : public CObject { public: virtual ~CException() {} virtual int ReportError(UINT = 0, UINT = 0) { return 0; } virtual BOOL GetErrorMessage(char *b, UINT n, UINT * = 0) { if (b && n) b[0] = 0; return FALSE; } void Delete() { delete this; } };
class CFileException : public CException { public: int m_cause = 0; LONG m_lOsError = 0; };
class CMemoryException : public CException {};

// --------------------------------------------------------------------- CString
class CString {
public:
    //  MFC's CString is ONE pointer to the character data. Keeping that layout
    //  costs nothing and matches the original, but it is NOT what makes
    //  printf("%s", someCString) work here: the Itanium ABI passes a non-trivial
    //  class through varargs by invisible reference, so those call sites were
    //  fixed to pass GetString() and -Wnon-pod-varargs keeps them fixed.
    char *m_pchData;

private:
    std::string s;
    void sync() { m_pchData = const_cast<char *>(s.c_str()); }

public:
    CString() { sync(); }
    CString(const char *p) : s(p ? p : "") { sync(); }
    CString(const char *p, int n) : s(p ? std::string(p, n) : std::string()) { sync(); }
    explicit CString(char c, int n = 1) : s((size_t)n, c) { sync(); }
    CString(const std::string &o) : s(o) { sync(); }
    CString(const CString &o) : s(o.s) { sync(); }
    CString(CString &&o) : s(std::move(o.s)) { sync(); o.sync(); }
    explicit CString(const wchar_t *w) { FromWide(w); }

    void FromWide(const wchar_t *w) {
        s.clear();
        if (w) for (; *w; ++w) s.push_back((char)(*w & 0xFF));
        sync();
    }

    operator const char *() const { return s.c_str(); }
    operator std::string() const { return s; }
    const char *GetString() const { return s.c_str(); }
    const std::string &Str() const { return s; }
    int GetLength() const { return (int)s.size(); }
    bool IsEmpty() const { return s.empty(); }
    void Empty() { s.clear(); sync(); }

    CString &operator=(const CString &o) { s = o.s; sync(); return *this; }
    CString &operator=(CString &&o) { s = std::move(o.s); sync(); o.sync(); return *this; }
    CString &operator=(const char *p) { s = p ? p : ""; sync(); return *this; }
    CString &operator=(const unsigned char *p) { s = p ? (const char *)p : ""; sync(); return *this; }
    CString &operator=(char c) { s.assign(1, c); sync(); return *this; }
    CString &operator+=(const CString &o) { s += o.s; sync(); return *this; }
    CString &operator+=(const char *p) { if (p) s += p; sync(); return *this; }
    CString &operator+=(char c) { s += c; sync(); return *this; }

    char operator[](int i) const { return s[(size_t)i]; }
    char GetAt(int i) const { return s[(size_t)i]; }
    void SetAt(int i, char c) { s[(size_t)i] = c; sync(); }

    int Compare(const char *p) const { return strcmp(s.c_str(), p ? p : ""); }
    int CompareNoCase(const char *p) const { return strcasecmp(s.c_str(), p ? p : ""); }
    int Collate(const char *p) const { return Compare(p); }
    int CollateNoCase(const char *p) const { return CompareNoCase(p); }

    CString Left(int n) const { n = (std::max)(0, (std::min)(n, GetLength())); return CString(s.substr(0, (size_t)n)); }
    CString Right(int n) const { n = (std::max)(0, (std::min)(n, GetLength())); return CString(s.substr(s.size() - (size_t)n)); }
    CString Mid(int i) const { if (i >= GetLength()) return CString(); return CString(s.substr((size_t)(std::max)(0, i))); }
    CString Mid(int i, int n) const {
        if (i >= GetLength() || n <= 0) return CString();
        return CString(s.substr((size_t)(std::max)(0, i), (size_t)n));
    }
    CString SpanExcluding(const char *set) const {
        size_t p = s.find_first_of(set ? set : "");
        return p == std::string::npos ? *this : CString(s.substr(0, p));
    }
    CString SpanIncluding(const char *set) const {
        size_t p = s.find_first_not_of(set ? set : "");
        return p == std::string::npos ? *this : CString(s.substr(0, p));
    }

    int Find(char c, int start = 0) const { size_t p = s.find(c, (size_t)(std::max)(0, start)); return p == std::string::npos ? -1 : (int)p; }
    int Find(const char *sub, int start = 0) const {
        if (!sub) return -1; size_t p = s.find(sub, (size_t)(std::max)(0, start));
        return p == std::string::npos ? -1 : (int)p;
    }
    int ReverseFind(char c) const { size_t p = s.rfind(c); return p == std::string::npos ? -1 : (int)p; }
    int FindOneOf(const char *set) const { size_t p = s.find_first_of(set ? set : ""); return p == std::string::npos ? -1 : (int)p; }

    void MakeUpper() { for (auto &c : s) c = (char)toupper((unsigned char)c); sync(); }
    void MakeLower() { for (auto &c : s) c = (char)tolower((unsigned char)c); sync(); }
    void MakeReverse() { std::reverse(s.begin(), s.end()); sync(); }
    CString &TrimLeft() { size_t p = s.find_first_not_of(" \t\r\n"); s = (p == std::string::npos) ? "" : s.substr(p); sync(); return *this; }
    void TrimLeft(char c) { size_t p = s.find_first_not_of(c); s = (p == std::string::npos) ? "" : s.substr(p); sync(); }
    CString &TrimRight() { size_t p = s.find_last_not_of(" \t\r\n"); s = (p == std::string::npos) ? "" : s.substr(0, p + 1); sync(); return *this; }
    void TrimRight(char c) { size_t p = s.find_last_not_of(c); s = (p == std::string::npos) ? "" : s.substr(0, p + 1); sync(); }
    void TrimLeft(const char *set) { size_t p = s.find_first_not_of(set ? set : ""); s = (p == std::string::npos) ? "" : s.substr(p); sync(); }
    void TrimRight(const char *set) { size_t p = s.find_last_not_of(set ? set : ""); s = (p == std::string::npos) ? "" : s.substr(0, p + 1); sync(); }
    CString &Trim() { TrimLeft(); TrimRight(); return *this; }
    void Append(const char *p) { if (p) s += p; sync(); }
    void Append(const CString &o) { s += o.s; sync(); }
    void AppendChar(char c) { s += c; sync(); }
    void Trim(const char *set) { TrimLeft(set); TrimRight(set); }
    void Trim(char c) { TrimLeft(c); TrimRight(c); }

    int Replace(const char *from, const char *to) {
        if (!from || !*from) return 0;
        int n = 0; size_t p = 0, fl = strlen(from), tl = to ? strlen(to) : 0;
        while ((p = s.find(from, p)) != std::string::npos) { s.replace(p, fl, to ? to : ""); p += tl; ++n; }
        sync();
        return n;
    }
    int Replace(char from, char to) { int n = 0; for (auto &c : s) if (c == from) { c = to; ++n; } sync(); return n; }
    int Remove(char c) { size_t before = s.size(); s.erase(std::remove(s.begin(), s.end(), c), s.end()); sync(); return (int)(before - s.size()); }
    int Insert(int i, const char *p) { if (p) s.insert((size_t)(std::min)((std::max)(0, i), GetLength()), p); sync(); return GetLength(); }
    int Insert(int i, char c) { s.insert((size_t)(std::min)((std::max)(0, i), GetLength()), 1, c); sync(); return GetLength(); }
    int Delete(int i, int n = 1) { if (i >= 0 && i < GetLength()) s.erase((size_t)i, (size_t)n); sync(); return GetLength(); }

    void Format(const char *fmt, ...) {
        va_list ap; va_start(ap, fmt); FormatV(fmt, ap); va_end(ap);
    }
    void AppendFormat(const char *fmt, ...) {
        CString t; va_list ap; va_start(ap, fmt); t.FormatV(fmt, ap); va_end(ap); s += t.s; sync();
    }
    void FormatV(const char *fmt, va_list ap) {
        //  "%I64d" is MSVC's spelling of a 64-bit conversion and bionic does
        //  not know it, so it has to be rewritten before either pass.
        std::string rewritten;
        if (RanFmt_NeedsRewrite(fmt)) { rewritten = RanFmt_MsvcToPosix(fmt); fmt = rewritten.c_str(); }
        //  Both passes work on copies: the measuring pass because it must, and
        //  the real one so the caller's list survives - callers here reuse it.
        va_list c; va_copy(c, ap);
        int n = vsnprintf(NULL, 0, fmt, c); va_end(c);
        if (n < 0) { s.clear(); sync(); return; }
        std::vector<char> buf((size_t)n + 1);
        va_list c2; va_copy(c2, ap);
        vsnprintf(buf.data(), buf.size(), fmt, c2); va_end(c2);
        s.assign(buf.data(), (size_t)n);
        sync();
    }
    // MFC allows Format(UINT nStringResource,...) — not used on mobile.

    char *GetBuffer(int minLen = 0) {
        if (minLen > GetLength()) s.resize((size_t)minLen, '\0');
        else if (s.capacity() < s.size() + 1) s.reserve(s.size() + 1);
        sync();
        return &s[0];
    }
    char *GetBufferSetLength(int len) { s.resize((size_t)(std::max)(0, len), '\0'); sync(); return &s[0]; }
    void ReleaseBuffer(int newLen = -1) {
        if (newLen < 0) s.resize(strlen(s.c_str()));
        else s.resize((size_t)newLen);
        sync();
    }
    void FreeExtra() { s.shrink_to_fit(); sync(); }
    void SetString(const char *p) { s = p ? p : ""; sync(); }
    int  GetAllocLength() const { return (int)s.capacity(); }
    static int StringLength(const char *p) { return p ? (int)strlen(p) : 0; }
    CString Tokenize(const char *sep, int &pos) const {
        if (pos < 0) return CString();
        size_t start = s.find_first_not_of(sep ? sep : "", (size_t)pos);
        if (start == std::string::npos) { pos = -1; return CString(); }
        size_t end = s.find_first_of(sep ? sep : "", start);
        if (end == std::string::npos) { pos = -1; return CString(s.substr(start)); }
        pos = (int)end + 1;
        return CString(s.substr(start, end - start));
    }

    friend CString operator+(const CString &a, const CString &b) { return CString(a.s + b.s); }
    friend CString operator+(const CString &a, const char *b) { return CString(a.s + (b ? b : "")); }
    friend CString operator+(const char *a, const CString &b) { return CString(std::string(a ? a : "") + b.s); }
    friend CString operator+(const CString &a, char b) { return CString(a.s + b); }
    friend bool operator==(const CString &a, const CString &b) { return a.s == b.s; }
    friend bool operator==(const CString &a, const char *b) { return a.s == (b ? b : ""); }
    friend bool operator==(const char *a, const CString &b) { return b.s == (a ? a : ""); }
    friend bool operator!=(const CString &a, const CString &b) { return a.s != b.s; }
    friend bool operator!=(const CString &a, const char *b) { return a.s != (b ? b : ""); }
    friend bool operator!=(const char *a, const CString &b) { return b.s != (a ? a : ""); }
    friend bool operator<(const CString &a, const CString &b) { return a.s < b.s; }
    friend bool operator>(const CString &a, const CString &b) { return a.s > b.s; }
    friend bool operator<=(const CString &a, const CString &b) { return a.s <= b.s; }
    friend bool operator>=(const CString &a, const CString &b) { return a.s >= b.s; }
};

typedef const CString &LPCTSTR_REF;

// ------------------------------------------------------- CPoint / CSize / CRect
struct CSize : public SIZE {
    CSize() { cx = cy = 0; }
    CSize(int x, int y) { cx = x; cy = y; }
    CSize(SIZE s) { cx = s.cx; cy = s.cy; }
    bool operator==(SIZE s) const { return cx == s.cx && cy == s.cy; }
    bool operator!=(SIZE s) const { return !(*this == s); }
};
struct CPoint : public POINT {
    CPoint() { x = y = 0; }
    CPoint(int X, int Y) { x = X; y = Y; }
    CPoint(POINT p) { x = p.x; y = p.y; }
    CPoint(DWORD dw) { x = (short)LOWORD(dw); y = (short)HIWORD(dw); }
    void Offset(int dx, int dy) { x += dx; y += dy; }
    bool operator==(POINT p) const { return x == p.x && y == p.y; }
    bool operator!=(POINT p) const { return !(*this == p); }
    CPoint operator+(SIZE s) const { return CPoint(x + s.cx, y + s.cy); }
    CPoint operator-(SIZE s) const { return CPoint(x - s.cx, y - s.cy); }
    CSize  operator-(POINT p) const { return CSize(x - p.x, y - p.y); }
};
struct CRect : public RECT {
    CRect() { left = top = right = bottom = 0; }
    CRect(int l, int t, int r, int b) { left = l; top = t; right = r; bottom = b; }
    CRect(const RECT &r) { left = r.left; top = r.top; right = r.right; bottom = r.bottom; }
    CRect(POINT tl, POINT br) { left = tl.x; top = tl.y; right = br.x; bottom = br.y; }
    CRect(POINT tl, SIZE sz) { left = tl.x; top = tl.y; right = tl.x + sz.cx; bottom = tl.y + sz.cy; }
    int Width() const { return right - left; }
    int Height() const { return bottom - top; }
    CSize Size() const { return CSize(Width(), Height()); }
    CPoint TopLeft() const { return CPoint(left, top); }
    CPoint BottomRight() const { return CPoint(right, bottom); }
    CPoint CenterPoint() const { return CPoint((left + right) / 2, (top + bottom) / 2); }
    bool IsRectEmpty() const { return Width() <= 0 || Height() <= 0; }
    bool IsRectNull() const { return !left && !top && !right && !bottom; }
    void SetRect(int l, int t, int r, int b) { left = l; top = t; right = r; bottom = b; }
    void SetRectEmpty() { left = top = right = bottom = 0; }
    void OffsetRect(int dx, int dy) { left += dx; right += dx; top += dy; bottom += dy; }
    void OffsetRect(POINT p) { OffsetRect(p.x, p.y); }
    void InflateRect(int dx, int dy) { left -= dx; right += dx; top -= dy; bottom += dy; }
    void DeflateRect(int dx, int dy) { InflateRect(-dx, -dy); }
    void NormalizeRect() { if (left > right) std::swap(left, right); if (top > bottom) std::swap(top, bottom); }
    bool PtInRect(POINT p) const { return p.x >= left && p.x < right && p.y >= top && p.y < bottom; }
    bool IntersectRect(const RECT *a, const RECT *b) {
        left = (std::max)(a->left, b->left); top = (std::max)(a->top, b->top);
        right = (std::min)(a->right, b->right); bottom = (std::min)(a->bottom, b->bottom);
        if (IsRectEmpty()) { SetRectEmpty(); return false; }
        return true;
    }
    bool UnionRect(const RECT *a, const RECT *b) {
        left = (std::min)(a->left, b->left); top = (std::min)(a->top, b->top);
        right = (std::max)(a->right, b->right); bottom = (std::max)(a->bottom, b->bottom);
        return !IsRectEmpty();
    }
    void CopyRect(const RECT *r) { *(RECT *)this = *r; }
    operator LPRECT() { return this; }
    operator LPCRECT() const { return this; }
    bool operator==(const RECT &r) const {
        return left == r.left && top == r.top && right == r.right && bottom == r.bottom;
    }
    bool operator!=(const RECT &r) const { return !(*this == r); }
};

// ----------------------------------------------------------------------- CTime
// MFC lays CTime/CTimeSpan out with 4-byte alignment on x86 (their __time64_t
// member does NOT force 8-byte alignment there). Wire structs embed CTime, so
// the shim must reproduce that or every packet holding one desyncs on arm64.
#pragma pack(push, 4)
class CTimeSpan {
public:
    int64_t m_span = 0;
    CTimeSpan() {}
    CTimeSpan(int64_t s) : m_span(s) {}
    CTimeSpan(LONG days, int hours, int mins, int secs)
        : m_span((int64_t)days * 86400 + hours * 3600 + mins * 60 + secs) {}
    int64_t GetTimeSpan() const { return m_span; }
    CString Format(const char *fmt) const;
    int64_t GetTotalSeconds() const { return m_span; }
    int64_t GetTotalMinutes() const { return m_span / 60; }
    int64_t GetTotalHours() const { return m_span / 3600; }
    LONG GetDays() const { return (LONG)(m_span / 86400); }
    int GetHours() const { return (int)((m_span / 3600) % 24); }
    int GetMinutes() const { return (int)((m_span / 60) % 60); }
    int GetSeconds() const { return (int)(m_span % 60); }
    CTimeSpan operator+(const CTimeSpan &o) const { return CTimeSpan(m_span + o.m_span); }
    CTimeSpan &operator+=(const CTimeSpan &o) { m_span += o.m_span; return *this; }
    CTimeSpan &operator-=(const CTimeSpan &o) { m_span -= o.m_span; return *this; }
    CTimeSpan operator-(const CTimeSpan &o) const { return CTimeSpan(m_span - o.m_span); }
    bool operator<(const CTimeSpan &o) const { return m_span < o.m_span; }
    bool operator>(const CTimeSpan &o) const { return m_span > o.m_span; }
    bool operator==(const CTimeSpan &o) const { return m_span == o.m_span; }
    bool operator!=(const CTimeSpan &o) const { return m_span != o.m_span; }
    bool operator<=(const CTimeSpan &o) const { return m_span <= o.m_span; }
    bool operator>=(const CTimeSpan &o) const { return m_span >= o.m_span; }
};

class CTime {
public:
    int64_t m_time = 0;
    CTime() {}
    CTime(int64_t t) : m_time(t) {}
    CTime(int y, int mo, int d, int h, int mi, int s, int dst = -1) {
        struct tm tmv; memset(&tmv, 0, sizeof(tmv));
        tmv.tm_year = y - 1900; tmv.tm_mon = mo - 1; tmv.tm_mday = d;
        tmv.tm_hour = h; tmv.tm_min = mi; tmv.tm_sec = s; tmv.tm_isdst = dst;
        m_time = (int64_t)mktime(&tmv);
    }
    CTime(const SYSTEMTIME &st, int dst = -1)
        : CTime(st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, dst) {}
    static CTime GetCurrentTime() { return CTime((int64_t)time(NULL)); }
    time_t GetTime() const { return (time_t)m_time; }
    struct tm Local() const { time_t t = (time_t)m_time; struct tm o; localtime_r(&t, &o); return o; }
    int GetYear() const { return Local().tm_year + 1900; }
    int GetMonth() const { return Local().tm_mon + 1; }
    int GetDay() const { return Local().tm_mday; }
    int GetHour() const { return Local().tm_hour; }
    int GetMinute() const { return Local().tm_min; }
    int GetSecond() const { return Local().tm_sec; }
    int GetDayOfWeek() const { return Local().tm_wday + 1; }
    struct tm *GetLocalTm(struct tm *p) const { *p = Local(); return p; }
    CString Format(const char *fmt) const {
        struct tm t = Local(); char buf[256];
        size_t n = strftime(buf, sizeof(buf), fmt, &t);
        return CString(buf, (int)n);
    }
    CTimeSpan operator-(const CTime &o) const { return CTimeSpan(m_time - o.m_time); }
    CTime operator-(const CTimeSpan &o) const { return CTime(m_time - o.m_span); }
    CTime operator+(const CTimeSpan &o) const { return CTime(m_time + o.m_span); }
    CTime &operator+=(const CTimeSpan &o) { m_time += o.m_span; return *this; }
    CTime &operator-=(const CTimeSpan &o) { m_time -= o.m_span; return *this; }
    bool operator<(const CTime &o) const { return m_time < o.m_time; }
    bool operator>(const CTime &o) const { return m_time > o.m_time; }
    bool operator==(const CTime &o) const { return m_time == o.m_time; }
    bool operator!=(const CTime &o) const { return m_time != o.m_time; }
    bool operator<=(const CTime &o) const { return m_time <= o.m_time; }
    bool operator>=(const CTime &o) const { return m_time >= o.m_time; }
};
#pragma pack(pop)
typedef CTime COleDateTime;

// ----------------------------------------------------------------------- CFile
class CFile : public CObject {
public:
    enum OpenFlags {
        modeRead = 0x0000, modeWrite = 0x0001, modeReadWrite = 0x0002,
        shareCompat = 0x0000, shareExclusive = 0x0010, shareDenyWrite = 0x0020,
        shareDenyRead = 0x0030, shareDenyNone = 0x0040,
        modeNoInherit = 0x0080, modeCreate = 0x1000, modeNoTruncate = 0x2000,
        typeText = 0x4000, typeBinary = (int)0x8000
    };
    enum SeekPosition { begin = SEEK_SET, current = SEEK_CUR, end = SEEK_END };
    FILE *m_fp = NULL;
    CString m_strFileName;

    CFile() {}
    CFile(const char *path, UINT flags) { Open(path, flags); }
    virtual ~CFile() { Close(); }

    virtual BOOL Open(const char *path, UINT flags, CFileException *ex = NULL) {
        const char *mode = "rb";
        if (flags & modeCreate) mode = (flags & modeNoTruncate) ? "r+b" : "wb";
        else if (flags & modeReadWrite) mode = "r+b";
        else if (flags & modeWrite) mode = "r+b";
        m_fp = fopen(RanPath_Resolve(path), mode);
        if (!m_fp && (flags & (modeWrite | modeReadWrite))) m_fp = fopen(RanPath_Resolve(path), "w+b");
        m_strFileName = path;
        if (!m_fp && ex) ex->m_lOsError = (LONG)errno;
        return m_fp != NULL;
    }
    virtual void Close() { if (m_fp) { fclose(m_fp); m_fp = NULL; } }
    virtual UINT Read(void *buf, UINT n) { return m_fp ? (UINT)fread(buf, 1, n, m_fp) : 0; }
    virtual void Write(const void *buf, UINT n) { if (m_fp) fwrite(buf, 1, n, m_fp); }
    virtual LONG Seek(LONG off, UINT from) { if (m_fp) fseek(m_fp, off, (int)from); return GetPosition(); }
    virtual void SeekToBegin() { if (m_fp) fseek(m_fp, 0, SEEK_SET); }
    virtual LONG SeekToEnd() { if (m_fp) fseek(m_fp, 0, SEEK_END); return GetPosition(); }
    virtual DWORD GetPosition() const { return m_fp ? (DWORD)ftell(m_fp) : 0; }
    virtual DWORD GetLength() const {
        if (!m_fp) return 0;
        long cur = ftell(m_fp); fseek(m_fp, 0, SEEK_END);
        long len = ftell(m_fp); fseek(m_fp, cur, SEEK_SET); return (DWORD)len;
    }
    virtual void Flush() { if (m_fp) fflush(m_fp); }
    const CString &GetFilePath() const { return m_strFileName; }
    const CString &GetFileName() const { return m_strFileName; }
};

class CStdioFile : public CFile {
public:
    CStdioFile() {}
    CStdioFile(const char *p, UINT f) : CFile(p, f) {}
    BOOL ReadString(CString &out) {
        if (!m_fp) return FALSE;
        char buf[4096];
        if (!fgets(buf, sizeof(buf), m_fp)) return FALSE;
        size_t n = strlen(buf);
        while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = 0;
        out = buf; return TRUE;
    }
    void WriteString(const char *s) { if (m_fp && s) fputs(s, m_fp); }
};

// -------------------------------------------------------------------- CFileFind
class CFileFind {
    // The directory is read up front so FindNextFile can report whether a
    // FURTHER entry exists, which is what MFC does and what the recursive
    // texture-tree walk relies on.
    std::vector<std::string> m_entries;
    int m_index = -1;
    CString m_pattern, m_root, m_name;
    bool m_isDir = false;
public:
    CFileFind();
    virtual ~CFileFind();
    BOOL FindFile(const char *pattern = NULL, DWORD unused = 0);
    BOOL FindNextFile();
    void Close();
    CString GetFileName() const { return m_name; }
    CString GetFilePath() const { return m_root + m_name; }
    CString GetFileTitle() const;
    BOOL IsDirectory() const { return m_isDir; }
    BOOL IsDots() const { return m_name == "." || m_name == ".."; }
    BOOL IsHidden() const { return !m_name.IsEmpty() && m_name[0] == '.'; }
};

// ------------------------------------------------------------------ collections
class CStringArray {
    std::vector<CString> v;
public:
    int GetSize() const { return (int)v.size(); }
    int GetCount() const { return (int)v.size(); }
    bool IsEmpty() const { return v.empty(); }
    int GetUpperBound() const { return (int)v.size() - 1; }
    void SetSize(int n, int = -1) { v.resize((size_t)(std::max)(0, n)); }
    void RemoveAll() { v.clear(); }
    void RemoveAt(int i, int n = 1) { if (i >= 0 && i < GetSize()) v.erase(v.begin() + i, v.begin() + (std::min)((size_t)(i + n), v.size())); }
    int Add(const CString &s) { v.push_back(s); return (int)v.size() - 1; }
    void InsertAt(int i, const CString &s) { v.insert(v.begin() + (std::min)((size_t)(std::max)(0, i), v.size()), s); }
    const CString &GetAt(int i) const { return v[(size_t)i]; }
    CString &GetAt(int i) { return v[(size_t)i]; }
    void SetAt(int i, const CString &s) { v[(size_t)i] = s; }
    void SetAtGrow(int i, const CString &s) { if (i >= GetSize()) v.resize((size_t)i + 1); v[(size_t)i] = s; }
    const CString &operator[](int i) const { return v[(size_t)i]; }
    CString &operator[](int i) { return v[(size_t)i]; }
};

template <class T, class ARG = T>
class CArray {
    std::vector<T> v;
public:
    int GetSize() const { return (int)v.size(); }
    int GetCount() const { return (int)v.size(); }
    bool IsEmpty() const { return v.empty(); }
    int GetUpperBound() const { return (int)v.size() - 1; }
    void SetSize(int n, int = -1) { v.resize((size_t)(std::max)(0, n)); }
    void RemoveAll() { v.clear(); }
    void RemoveAt(int i, int n = 1) { if (i >= 0 && i < GetSize()) v.erase(v.begin() + i, v.begin() + (std::min)((size_t)(i + n), v.size())); }
    int Add(ARG x) { v.push_back(x); return (int)v.size() - 1; }
    void InsertAt(int i, ARG x) { v.insert(v.begin() + (std::min)((size_t)(std::max)(0, i), v.size()), x); }
    const T &GetAt(int i) const { return v[(size_t)i]; }
    T &GetAt(int i) { return v[(size_t)i]; }
    void SetAt(int i, ARG x) { v[(size_t)i] = x; }
    void SetAtGrow(int i, ARG x) { if (i >= GetSize()) v.resize((size_t)i + 1); v[(size_t)i] = x; }
    const T &operator[](int i) const { return v[(size_t)i]; }
    T &operator[](int i) { return v[(size_t)i]; }
    T *GetData() { return v.data(); }
};

typedef void *POSITION;

template <class T, class ARG = T>
class CList {
    std::list<T> l;
public:
    int GetCount() const { return (int)l.size(); }
    bool IsEmpty() const { return l.empty(); }
    void RemoveAll() { l.clear(); }
    POSITION AddHead(ARG x) { l.push_front(x); return (POSITION)&l.front(); }
    POSITION AddTail(ARG x) { l.push_back(x); return (POSITION)&l.back(); }
    T RemoveHead() { T x = l.front(); l.pop_front(); return x; }
    T RemoveTail() { T x = l.back(); l.pop_back(); return x; }
    T &GetHead() { return l.front(); }
    T &GetTail() { return l.back(); }
    POSITION GetHeadPosition() const { return l.empty() ? NULL : (POSITION) new typename std::list<T>::const_iterator(l.begin()); }
    T GetNext(POSITION &pos) const {
        typedef typename std::list<T>::const_iterator It;
        It *it = (It *)pos; T x = **it; ++(*it);
        if (*it == l.end()) { delete it; pos = NULL; }
        return x;
    }
    void RemoveAt(POSITION) {}
};

class CMapStringToString {
    std::map<std::string, std::string> m;
public:
    int GetCount() const { return (int)m.size(); }
    bool IsEmpty() const { return m.empty(); }
    void RemoveAll() { m.clear(); }
    void SetAt(const char *k, const char *v) { m[k ? k : ""] = v ? v : ""; }
    BOOL Lookup(const char *k, CString &out) const {
        auto it = m.find(k ? k : ""); if (it == m.end()) return FALSE;
        out = it->second.c_str(); return TRUE;
    }
    BOOL RemoveKey(const char *k) { return m.erase(k ? k : "") > 0; }
    CString &operator[](const char *k) { static CString tmp; tmp = m[k ? k : ""].c_str(); return tmp; }
    POSITION GetStartPosition() const { return m.empty() ? NULL : (POSITION)1; }
};

template <class KEY, class ARG_KEY, class VALUE, class ARG_VALUE>
class CMap {
    std::map<KEY, VALUE> m;
public:
    int GetCount() const { return (int)m.size(); }
    bool IsEmpty() const { return m.empty(); }
    void RemoveAll() { m.clear(); }
    void SetAt(ARG_KEY k, ARG_VALUE v) { m[k] = v; }
    BOOL Lookup(ARG_KEY k, VALUE &out) const {
        auto it = m.find(k); if (it == m.end()) return FALSE; out = it->second; return TRUE;
    }
    BOOL RemoveKey(ARG_KEY k) { return m.erase(k) > 0; }
    VALUE &operator[](ARG_KEY k) { return m[k]; }
};
class CMapStringToPtr {
    std::map<std::string, void *> m;
public:
    int GetCount() const { return (int)m.size(); }
    bool IsEmpty() const { return m.empty(); }
    void RemoveAll() { m.clear(); }
    void SetAt(const char *k, void *v) { m[k ? k : ""] = v; }
    BOOL Lookup(const char *k, void *&out) const {
        auto it = m.find(k ? k : ""); if (it == m.end()) return FALSE; out = it->second; return TRUE;
    }
    BOOL RemoveKey(const char *k) { return m.erase(k ? k : "") > 0; }
    void *&operator[](const char *k) { return m[k ? k : ""]; }
};

// ------------------------------------------------------------- synchronisation
class CSyncObject { public: virtual ~CSyncObject() {} virtual BOOL Lock(DWORD = INFINITE) { return TRUE; } virtual BOOL Unlock() { return TRUE; } };
class CCriticalSection : public CSyncObject {
    CRITICAL_SECTION m_cs;
public:
    CCriticalSection() { InitializeCriticalSection(&m_cs); }
    ~CCriticalSection() { DeleteCriticalSection(&m_cs); }
    BOOL Lock(DWORD = INFINITE) { EnterCriticalSection(&m_cs); return TRUE; }
    BOOL Unlock() { LeaveCriticalSection(&m_cs); return TRUE; }
};
class CSingleLock {
    CSyncObject *m_o; bool m_locked = false;
public:
    CSingleLock(CSyncObject *o, BOOL lockNow = FALSE) : m_o(o) { if (lockNow) Lock(); }
    ~CSingleLock() { Unlock(); }
    BOOL Lock(DWORD t = INFINITE) { if (m_o && !m_locked) { m_o->Lock(t); m_locked = true; } return TRUE; }
    BOOL Unlock() { if (m_o && m_locked) { m_o->Unlock(); m_locked = false; } return TRUE; }
    BOOL IsLocked() const { return m_locked; }
};

// ------------------------------------------------------------ inert GUI stubs
// The client only reaches these on paths that are dead on mobile (editor dialogs,
// IE embed, desktop cursor). They compile and do nothing.
class CDC {
public:
    HDC m_hDC = NULL;
    operator HDC() const { return m_hDC; }
    HDC GetSafeHdc() const { return m_hDC; }
    int SetBkMode(int) { return 0; }
    COLORREF SetTextColor(COLORREF) { return 0; }
    COLORREF SetBkColor(COLORREF) { return 0; }
    BOOL TextOut(int, int, const char *, int) { return TRUE; }
    BOOL TextOut(int, int, const CString &) { return TRUE; }
    void *SelectObject(void *) { return NULL; }
    BOOL MoveTo(int, int) { return TRUE; }
    BOOL LineTo(int, int) { return TRUE; }
    BOOL Rectangle(int, int, int, int) { return TRUE; }
    BOOL FillSolidRect(const RECT *, COLORREF) { return TRUE; }
    BOOL BitBlt(int, int, int, int, CDC *, int, int, DWORD) { return TRUE; }
    CSize GetTextExtent(const char *, int = -1) const { return CSize(0, 0); }
};
class CGdiObject { public: void *m_h = NULL; BOOL DeleteObject() { return TRUE; } };
class CPen : public CGdiObject { public: CPen() {} CPen(int, int, COLORREF) {} BOOL CreatePen(int, int, COLORREF) { return TRUE; } };
class CBrush : public CGdiObject { public: CBrush() {} CBrush(COLORREF) {} BOOL CreateSolidBrush(COLORREF) { return TRUE; } };
class CFont : public CGdiObject { public: BOOL CreateFontIndirect(const void *) { return TRUE; } };
class CBitmap : public CGdiObject { public: HBITMAP m_hObject = NULL; BOOL LoadBitmap(const char *) { return TRUE; } int GetBitmap(void *) { return 0; } DWORD GetBitmapBits(DWORD, void *) const { return 0; } BOOL Attach(HBITMAP h) { m_hObject = h; return TRUE; } };
class CPalette : public CGdiObject {};

class CWnd : public CObject {
public:
    //  Not NULL, deliberately. There IS a window here - the Android surface -
    //  and the handle is inert only because every Win32 call in the shim
    //  ignores it. Code that asks "do I have a window" is asking a real
    //  question, and NULL answered it wrongly: DxSoundMan::OneTimeSceneInit
    //  returns before it initialises anything if the handle is null, which is
    //  why the game had no sound at all.
    HWND m_hWnd = (HWND) 1;
    CWnd() {}
    virtual ~CWnd() {}
    HWND GetSafeHwnd() const { return m_hWnd; }
    operator HWND() const { return m_hWnd; }
    void Attach(HWND h) { m_hWnd = h; }
    HWND Detach() { HWND h = m_hWnd; m_hWnd = NULL; return h; }
    BOOL GetClientRect(LPRECT r) const { return ::GetClientRect(m_hWnd, r); }
    BOOL GetWindowRect(LPRECT r) const { return ::GetWindowRect(m_hWnd, r); }
    BOOL ClientToScreen(LPPOINT p) const { return ::ClientToScreen(m_hWnd, p); }
    BOOL ScreenToClient(LPPOINT p) const { return ::ScreenToClient(m_hWnd, p); }
    BOOL ShowWindow(int c) { return ::ShowWindow(m_hWnd, c); }
    BOOL UpdateWindow() { return ::UpdateWindow(m_hWnd); }
    BOOL InvalidateRect(const RECT *r = NULL, BOOL e = TRUE) { return ::InvalidateRect(m_hWnd, r, e); }
    LRESULT SendMessage(UINT m, WPARAM w = 0, LPARAM l = 0) { return ::SendMessageA(m_hWnd, m, w, l); }
    BOOL PostMessage(UINT m, WPARAM w = 0, LPARAM l = 0) { return ::PostMessageA(m_hWnd, m, w, l); }
    BOOL SetWindowPos(const CWnd *, int x, int y, int cx, int cy, UINT f) { return ::SetWindowPos(m_hWnd, NULL, x, y, cx, cy, f); }
    void SetWindowText(const char *) {}
    void GetWindowText(CString &out) const { out.Empty(); }
    CDC *GetDC() { static CDC dc; return &dc; }
    int ReleaseDC(CDC *) { return 1; }
    CWnd *GetParent() const { return NULL; }
    BOOL IsWindowVisible() const { return TRUE; }
    CWnd *SetFocus() { return NULL; }
    void MoveWindow(int, int, int, int, BOOL = TRUE) {}
    void MoveWindow(const RECT *, BOOL = TRUE) {}
    void EnableWindow(BOOL = TRUE) {}
    void DestroyWindow() {}
    static CWnd *FromHandle(HWND h) { static CWnd w; w.m_hWnd = h; return &w; }
    static CWnd *GetDesktopWindow() { return NULL; }
};
class CFrameWnd : public CWnd {};
class CDialog : public CWnd { public: CDialog() {} CDialog(UINT, CWnd * = NULL) {} virtual BOOL OnInitDialog() { return TRUE; } virtual void OnOK() {} virtual void OnCancel() {} int DoModal() { return IDCANCEL; } };
class CWinThread : public CObject { public: virtual BOOL InitInstance() { return TRUE; } virtual int ExitInstance() { return 0; } };
class CWinApp : public CWinThread {
public:
    HINSTANCE m_hInstance = NULL;
    CWnd *m_pMainWnd = NULL;
    const char *m_pszAppName = "RAN";
    virtual BOOL InitInstance() { return TRUE; }
    virtual int  ExitInstance() { return 0; }
    virtual int  Run() { return 0; }
    virtual BOOL PumpMessage() { return TRUE; }
};
CWinApp *AfxGetApp();
HINSTANCE AfxGetInstanceHandle();
CWnd *AfxGetMainWnd();
void AfxMessageBox(const char *text, UINT type = MB_OK);
void AfxThrowFileException(int cause, LONG osErr = 0, const char *name = NULL);
void AfxThrowMemoryException();

// Internet classes: dead on mobile (patcher/web board). Compile as failing stubs.
class CInternetSession {
public:
    CInternetSession(const char * = NULL, DWORD = 1, DWORD = 0, const char * = NULL,
                     const char * = NULL, DWORD = 0) {}
    virtual ~CInternetSession() {}
    void *OpenURL(const char *, DWORD = 1, DWORD = 0, const char * = NULL, DWORD = 0) { return NULL; }
    void Close() {}
    void SetOption(DWORD, DWORD) {}
};
class CHttpFile {
public:
    UINT Read(void *, UINT) { return 0; }
    BOOL QueryInfoStatusCode(DWORD &code) { code = 0; return FALSE; }
    void Close() {}
};
class CInternetException : public CException {};


// extra MFC odds and ends
class CEdit : public CWnd {
public:
    void GetWindowText(CString &s) const { s.Empty(); }
    void SetWindowText(const char *) {}
    int  GetSel() const { return 0; }
    void SetSel(int, int) {}
    void ReplaceSel(const char *) {}
    int  LineLength(int = -1) const { return 0; }
    int  GetLineCount() const { return 1; }
};
class CStatic : public CWnd {};
class CButton : public CWnd {};
class CListBox : public CWnd {};
class CComboBox : public CWnd {};
class CScrollBar : public CWnd {};
class CView : public CWnd {};
class CCreateContext {};
class CStringList : public CList<CString, const CString &> {};
class CObList : public CList<CObject *, CObject *> {};
class CPtrList : public CList<void *, void *> {};
class CPtrArray : public CArray<void *, void *> {};
class CDWordArray : public CArray<DWORD, DWORD> {};
class CUIntArray : public CArray<UINT, UINT> {};
class CByteArray : public CArray<BYTE, BYTE> {};

// late CString additions (MFC 8+ API used by the client)
inline int RanCStringLength(const CString &s) { return s.GetLength(); }
inline void AfxThrowUserException() { throw CException(); }
typedef WORD INTERNET_PORT;
inline BOOL AfxParseURL(const char *, DWORD &s, CString &, CString &, INTERNET_PORT &) { s = 0; return FALSE; }
inline void AfxAbort() { abort(); }
class CFileDialog : public CDialog {
public:
    CFileDialog(BOOL, const char * = 0, const char * = 0, DWORD = 0, const char * = 0, CWnd * = 0) {}
    CString GetPathName() const { return CString(); }
    CString GetFileName() const { return CString(); }
    CString GetFileTitle() const { return CString(); }
    int DoModal() { return IDCANCEL; }
    struct { DWORD Flags = 0, nMaxFile = 0, nFilterIndex = 0; const char *lpstrTitle = 0, *lpstrInitialDir = 0, *lpstrFilter = 0, *lpstrDefExt = 0; char *lpstrFile = 0; } m_ofn;
};
class CStringW {
public:
    std::wstring s;
    CStringW() {}
    CStringW(const wchar_t *p) : s(p ? p : L"") {}
    CStringW(const CStringW &o) : s(o.s) {}
    CStringW &operator=(const wchar_t *p) { s = p ? p : L""; return *this; }
    CStringW &operator=(const CStringW &o) { s = o.s; return *this; }
    CStringW &operator+=(const wchar_t *p) { if (p) s += p; return *this; }
    operator const wchar_t *() const { return s.c_str(); }
    const wchar_t *GetString() const { return s.c_str(); }
    int GetLength() const { return (int)s.size(); }
    bool IsEmpty() const { return s.empty(); }
    void Empty() { s.clear(); }
    wchar_t operator[](int i) const { return s[(size_t)i]; }
    wchar_t GetAt(int i) const { return s[(size_t)i]; }
    CStringW Left(int n) const { CStringW o; if (n > 0) o.s = s.substr(0, (size_t)((n < GetLength()) ? n : GetLength())); return o; }
    CStringW Right(int n) const { CStringW o; if (n > 0) { int k = (n < GetLength()) ? n : GetLength(); o.s = s.substr(s.size() - (size_t)k); } return o; }
    CStringW Mid(int i, int n = -1) const { CStringW o; if (i < GetLength()) o.s = (n < 0) ? s.substr((size_t)i) : s.substr((size_t)i, (size_t)n); return o; }
};

// CTimeSpan::Format (MFC-style %D/%H/%M/%S)
inline CString RanFormatTimeSpan(const CTimeSpan &ts, const char *fmt) {
    std::string f = fmt ? fmt : "", r;
    char buf[64];
    for (size_t i = 0; i < f.size(); ++i) {
        if (f[i] != '%' || i + 1 >= f.size()) { r += f[i]; continue; }
        switch (f[++i]) {
            case 'D': snprintf(buf, sizeof(buf), "%ld", (long)ts.GetDays()); r += buf; break;
            case 'H': snprintf(buf, sizeof(buf), "%02d", ts.GetHours()); r += buf; break;
            case 'M': snprintf(buf, sizeof(buf), "%02d", ts.GetMinutes()); r += buf; break;
            case 'S': snprintf(buf, sizeof(buf), "%02d", ts.GetSeconds()); r += buf; break;
            case '%': r += '%'; break;
            default:  r += '%'; r += f[i]; break;
        }
    }
    return CString(r);
}
inline CString CTimeSpan::Format(const char *fmt) const { return RanFormatTimeSpan(*this, fmt); }

#endif // RAN_MFC_COMPAT_H
