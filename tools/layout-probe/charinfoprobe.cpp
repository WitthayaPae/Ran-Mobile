// charinfoprobe — MSVC offsetof for SCHARINFO_LOBBY, the per-slot character
// detail carried by NET_MSG_LOBBY_CHAR_SEL (2332, GLMSG::SNETLOBBY_CHARINFO).
//
// The char-select screen needs each character's LEVEL and CLASS/SCHOOL. Those
// live in SCHARINFO_LOBBY, which is defined at FILE SCOPE in GLContrlBaseMsg.h
// with NATURAL alignment — and is then embedded as `Data` inside the
// #pragma pack(1) message SNETLOBBY_CHARINFO. pack(1) governs where `Data` sits
// in the message (no padding: it lands right after the 8-byte NET_MSG_GENERIC),
// but the INTERNAL layout of SCHARINFO_LOBBY keeps its own natural alignment,
// so its member offsets are exactly the kind of thing that must be measured,
// never hand-summed. This prints ground truth for RanLoginFlow.AddCharName.
//
// Same rule as the other probes: SOURCE/ is frozen — this only #includes it.
//
// Build/run:  charinfoprobe.cmd
//
// Recorded output (VS 2022, x86), which RanLoginFlow pins and RunCheck tests:
//   SCHARINFO_LOBBY size = ...   SNETLOBBY_CHARINFO size = ...
//   NET_MSG_GENERIC size = 8     CHAR_SZNAME = 33   sizeof(EMCHARCLASS) = 4
//   Data 8   m_dwCharID 0   m_szName 4   m_emClass 40   m_wSchool 44
//   m_wHair 46   m_wFace 48   m_sHP 56   m_sExperience 64   m_nBright 72
//   m_wLevel 76
// The NET_MSG_GENERIC header is stripped before the body reaches the handler,
// so a body offset equals the SCHARINFO_LOBBY offset above (Data sits at body 0).

#ifndef WINVER
#define WINVER 0x0501
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif
#define WIN32_LEAN_AND_MEAN
#define VC_EXTRALEAN
#define _CRT_SECURE_NO_WARNINGS
#define _ATL_CSTRING_EXPLICIT_CONSTRUCTORS

#include <afx.h>
#include <afxwin.h>
#include <afxext.h>
#include <afxdisp.h>
#include <afxcoll.h>
#include "dxstdafx.h"

#include <cstdio>
#include <cstddef>
#include <string>
#include <vector>
#include <map>
#include <strsafe.h>
#include "d3dx9.h"

#include "s_NetGlobal.h"
#include "GLContrlPcMsg.h"
#include "GLContrlCharJoinMsg.h"   // pulls in GLContrlBaseMsg.h (SCHARINFO_LOBBY)

#define O(f) ((int)offsetof(SCHARINFO_LOBBY, f))

int main()
{
    printf("SCHARINFO_LOBBY size=%d\n", (int)sizeof(SCHARINFO_LOBBY));
    printf("SNETLOBBY_CHARINFO size=%d\n", (int)sizeof(GLMSG::SNETLOBBY_CHARINFO));
    printf("NET_MSG_GENERIC size=%d\n", (int)sizeof(NET_MSG_GENERIC));
    printf("CHAR_SZNAME=%d\n", (int)CHAR_SZNAME);
    printf("sizeof(EMCHARCLASS)=%d\n", (int)sizeof(EMCHARCLASS));
    printf("Data=%d\n", (int)offsetof(GLMSG::SNETLOBBY_CHARINFO, Data));
    printf("m_dwCharID=%d\n", O(m_dwCharID));
    printf("m_szName=%d\n", O(m_szName));
    printf("m_emClass=%d\n", O(m_emClass));
    printf("m_wSchool=%d\n", O(m_wSchool));
    printf("m_wHair=%d\n", O(m_wHair));
    printf("m_wFace=%d\n", O(m_wFace));
    printf("m_wSex=%d\n", O(m_wSex));
    printf("m_wHairColor=%d\n", O(m_wHairColor));
    printf("m_sHP=%d\n", O(m_sHP));
    printf("m_sExperience=%d\n", O(m_sExperience));
    printf("m_nBright=%d\n", O(m_nBright));
    printf("m_wLevel=%d\n", O(m_wLevel));
    printf("m_sStats=%d\n", O(m_sStats));
    printf("m_fScaleRange=%d\n", O(m_fScaleRange));
    return 0;
}
