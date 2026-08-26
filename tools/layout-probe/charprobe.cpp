// charprobe — MSVC offsetof for SDROP_CHAR, the DROP_PC payload.
//
// The main layout probe does not scan GLCharData.h (adding it would pull the
// whole character-data header into layout.json), so SDROP_CHAR's offsets are
// measured here instead. Same rule as the main probe: SOURCE/ is frozen — this
// only #includes it and prints ground truth.
//
// Build/run:  charprobe.cmd
//
// Recorded output (VS 2022, x86), which RanDropReader pins and RunCheck tests:
//   SDROP_CHAR size = 1720   SNETDROP_PC size = 1728   NET_MSG_GENERIC size = 8
//   CHAR_SZNAME = 33
//   szName 0  emTribe 36  emClass 40  wSchool 44  wSex 52  nBright 56
//   dwCharID 60  sHP 164  dwGaeaID 172  sMapID 176  dwCeID 180
//   vPos 184  vDir 196  Action 208  vTarPos 216  m_fScaleRange 1608
// The NET_MSG_GENERIC header is stripped before the body reaches the handler,
// so a body offset equals the struct offset above.

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
#include "GLContrlCharJoinMsg.h"
#include "GLContrlMsg.h"
#include "GLContrlCrowMsg.h"
#include "GLCrowData.h"
#include "GLCharData.h"

#define O(f) ((int)offsetof(SDROP_CHAR, f))

int main()
{
    printf("SDROP_CHAR size=%d\n", (int)sizeof(SDROP_CHAR));
    printf("SNETDROP_PC size=%d\n", (int)sizeof(GLMSG::SNETDROP_PC));
    printf("NET_MSG_GENERIC size=%d\n", (int)sizeof(NET_MSG_GENERIC));
    printf("CHAR_SZNAME=%d\n", (int)CHAR_SZNAME);
    printf("szName=%d\n", O(szName));
    printf("emTribe=%d\n", O(emTribe));
    printf("emClass=%d\n", O(emClass));
    printf("wSchool=%d\n", O(wSchool));
    printf("wSex=%d\n", O(wSex));
    printf("nBright=%d\n", O(nBright));
    printf("dwCharID=%d\n", O(dwCharID));
    printf("sHP=%d\n", O(sHP));
    printf("dwGaeaID=%d\n", O(dwGaeaID));
    printf("sMapID=%d\n", O(sMapID));
    printf("dwCeID=%d\n", O(dwCeID));
    printf("vPos=%d\n", O(vPos));
    printf("vDir=%d\n", O(vDir));
    printf("Action=%d\n", O(Action));
    printf("vTarPos=%d\n", O(vTarPos));
    printf("m_fScaleRange=%d\n", O(m_fScaleRange));

    // --- CHAR_JOIN (2333) spawn packet: where m_emClass lives -----------------
    // The client casts the 2333 body to GLMSG::SNETLOBBY_CHARJOIN and reads
    // pCharJoin->Data.m_emClass (DxGameStage.cpp:525/533). Data is an SCHARDATA
    // embedded in the pack(1) message; SCHARDATA itself is NATURALLY aligned, so
    // m_emClass sits at its own natural offset, NOT the SCHARINFO_LOBBY offset.
    // Body offset (header stripped) = off(Data) - sizeof(NET_MSG_GENERIC)
    //                                 + off(SCHARDATA::m_emClass).
    int joinDataOff = (int)offsetof(GLMSG::SNETLOBBY_CHARJOIN, Data);
    int schardataClassOff = (int)offsetof(SCHARDATA, m_emClass);
    int hdr = (int)sizeof(NET_MSG_GENERIC);
    printf("SNETLOBBY_CHARJOIN size=%d\n", (int)sizeof(GLMSG::SNETLOBBY_CHARJOIN));
    printf("CHARJOIN.Data=%d\n", joinDataOff);
    printf("SCHARDATA.m_emClass=%d\n", schardataClassOff);
    printf("SCHARDATA.m_szName=%d\n", (int)offsetof(SCHARDATA, m_szName));
    printf("SCHARDATA.m_wSex=%d\n", (int)offsetof(SCHARDATA, m_wSex));
    printf("SCHARDATA.m_dwCharID=%d\n", (int)offsetof(SCHARDATA, m_dwCharID));
    printf("CHARJOIN_BODY.emClass=%d\n", joinDataOff - hdr + schardataClassOff);
    printf("CHARJOIN_BODY.dwCharID=%d\n", joinDataOff - hdr + (int)offsetof(SCHARDATA, m_dwCharID));
    return 0;
}
