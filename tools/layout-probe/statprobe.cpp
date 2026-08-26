// statprobe — MSVC offsetof for the CHARACTER-STAT wire fields.
//
// The main layout probe deliberately does not scan GLCharData.h (adding the
// whole character-data header to layout.json is heavy), so — exactly like
// charprobe.cpp does for SDROP_CHAR — the SCHARDATA offsets the character-stat
// window needs are measured here instead. Same rule as every probe here:
// SOURCE/ is frozen; this only #includes it and prints ground truth.
//
// Build/run:  statprobe.cmd
//
// What this pins:
//   * SCHARDATA — the full own-character record carried by SNETLOBBY_CHARJOIN
//     (NET_MSG_LOBBY_CHAR_JOIN, 2333). m_sStats is the SCHARSTATS base block,
//     m_wStatsPoint the unspent points, m_wAP/m_wDP/m_wPA/m_wSA/m_wMA the derived
//     combat values, m_sExperience the level/exp pair, m_wLevel the level.
//   * The offset of SNETLOBBY_CHARJOIN::Data, so an absolute body offset is
//     (Data offset - 8 header) + (field offset within SCHARDATA).  SCHARDATA is
//     naturally aligned INSIDE a #pragma pack(1) message, so its interior offsets
//     are the standalone ones printed here (the SCHARINFO_LOBBY trap, restated).
//   * GLDWDATA / GLLLDATA strides (the (cur,max) pairs).

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
#include "GLCharData.h"

#define D(f) ((int)offsetof(SCHARDATA, f))

int main()
{
    printf("SCHARDATA size=%d\n", (int)sizeof(SCHARDATA));
    printf("SCHARSTATS size=%d\n", (int)sizeof(SCHARSTATS));
    printf("GLDWDATA size=%d\n", (int)sizeof(GLDWDATA));
    printf("GLLLDATA size=%d\n", (int)sizeof(GLLLDATA));
    printf("CHAR_SZNAME=%d\n", (int)CHAR_SZNAME);

    // Where SCHARDATA sits inside the char-join message.
    printf("CHARJOIN.Data=%d\n", (int)offsetof(GLMSG::SNETLOBBY_CHARJOIN, Data));
    printf("CHARJOIN size=%d\n", (int)sizeof(GLMSG::SNETLOBBY_CHARJOIN));

    // Identity / class / level.
    printf("m_dwCharID=%d\n",      D(m_dwCharID));
    printf("m_szName=%d\n",        D(m_szName));
    printf("m_emClass=%d\n",       D(m_emClass));
    printf("m_wSchool=%d\n",       D(m_wSchool));
    printf("m_wLevel=%d\n",        D(m_wLevel));

    // Base stat block (SCHARSTATS) + unspent points.
    printf("m_sStats=%d\n",        D(m_sStats));
    printf("m_wStatsPoint=%d\n",   D(m_wStatsPoint));

    // Derived combat values.
    printf("m_wAP=%d\n",           D(m_wAP));
    printf("m_wDP=%d\n",           D(m_wDP));
    printf("m_wPA=%d\n",           D(m_wPA));
    printf("m_wSA=%d\n",           D(m_wSA));
    printf("m_wMA=%d\n",           D(m_wMA));

    // Level / experience.
    printf("m_sExperience=%d\n",   D(m_sExperience));
    printf("m_lnReExp=%d\n",       D(m_lnReExp));
    printf("m_dwSkillPoint=%d\n",  D(m_dwSkillPoint));

    // Vital pools (also carried live in SNETPC_UPDATE_STATE / RanPlayerState).
    printf("m_sHP=%d\n",           D(m_sHP));
    printf("m_sMP=%d\n",           D(m_sMP));
    printf("m_sSP=%d\n",           D(m_sSP));
    printf("m_sCombatPoint=%d\n",  D(m_sCombatPoint));
    printf("m_wPK=%d\n",           D(m_wPK));
    return 0;
}
