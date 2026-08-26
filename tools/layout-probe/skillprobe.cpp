// skillprobe — MSVC offsetof/sizeof for the learned-skill-list wire struct.
//
// SCHARSKILL (GLCharData.h) is defined at NATURAL alignment (no pragma pack
// of its own) but is used as an array element inside SNETLOBBY_CHARSKILL
// (GLContrlCharJoinMsg.h), which lives inside that file's own #pragma
// pack(1) region (line 190-1293). SCHARSKILL's own layout is fixed at
// whatever pack state is active in the TU at the point GLCharData.h is
// actually included — SNATIVEID unions a DWORD, so under natural alignment
// SCHARSKILL pads 6 raw bytes up to 8; under pack(1) it stays 6. That is a
// real ambiguity only the real compiler, on the real include graph, can
// settle — not something to assume either way.
//
// This probe includes GLCharData.h BEFORE GLContrlCharJoinMsg.h, matching
// every real consumer .cpp (GLCharEx.cpp, GLCharData.cpp, ...), which all
// pull in GLCharData.h via GLChar.h/GLGaeaServer.h ahead of the network
// message headers. SOURCE/ is frozen — this only #includes it and prints
// ground truth. Build/run: skillprobe.cmd
//
// Recorded output is pinned by RanFriendPackets' companion skill-list
// packet reader and exercised by RunCheck.

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
#include "GLItem.h"
#include "GLItemDef.h"
#include "GLCharData.h"
#include "GLContrlPcMsg.h"
#include "GLContrlCharJoinMsg.h"

using namespace GLMSG;

#define OF(T, f) ((int)offsetof(T, f))

int main()
{
    printf("--- SNATIVEID / SCHARSKILL (GLCharData.h, natural alignment at include point) ---\n");
    printf("SNATIVEID size=%d wMainID=%d wSubID=%d\n",
        (int)sizeof(SNATIVEID), OF(SNATIVEID, wMainID), OF(SNATIVEID, wSubID));
    printf("SCHARSKILL size=%d sNativeID=%d wLevel=%d\n",
        (int)sizeof(SCHARSKILL), OF(SCHARSKILL, sNativeID), OF(SCHARSKILL, wLevel));

    printf("--- SNETLOBBY_CHARSKILL (GLContrlCharJoinMsg.h, pack(1) region) ---\n");
    printf("SNETLOBBY_CHARSKILL size=%d wNum=%d sSKILL=%d\n",
        (int)sizeof(SNETLOBBY_CHARSKILL), OF(SNETLOBBY_CHARSKILL, wNum),
        OF(SNETLOBBY_CHARSKILL, sSKILL));
    printf("sSKILL[1]-sSKILL[0] stride=%d\n",
        (int)((char*)&(((SNETLOBBY_CHARSKILL*)0)->sSKILL[1]) -
              (char*)&(((SNETLOBBY_CHARSKILL*)0)->sSKILL[0])));
    printf("EMGLMSG_SKILLMAX=%d\n", (int)EMGLMSG_SKILLMAX);
    return 0;
}
