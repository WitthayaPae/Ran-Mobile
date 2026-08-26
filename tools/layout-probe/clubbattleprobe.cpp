// clubbattleprobe — MSVC offsetof for the CLUB-BATTLE (guild-war) wire structs.
//
// The main layout probe (probe.cpp -> layout.json) already measures the GLMSG
// club-battle *messages* (SNETLOBBY_CLUB_BATTLE, SNET_CLUB_BATTLE_BEGIN_CLT2,
// SNET_CLUB_BATTLE_KILL_UPDATE, SNET_CLUB_BATTLE_POINT_UPDATE,
// SNET_CLUB_BATTLE_OVER_CLT, SNETLOBBY_CLUB_INFO). What it does NOT dump is the
// interior layout of GLCLUBBATTLE_LOBY, the per-opponent record embedded 10x in
// SNETLOBBY_CLUB_BATTLE::sBATTLE. That struct is defined in GLClubMan.h at
// NATURAL alignment (no #pragma pack), so its interior offsets differ from a
// naive pack(1) sum — exactly the SCHARINFO_LOBBY trap. Measure, do not assume.
//
// Same rule as every probe here: SOURCE/ is frozen; this only #includes it and
// prints ground truth. Build/run:  clubbattleprobe.cmd
//
// GLCLUBBATTLE_LOBY (GLClubMan.h:232):
//   DWORD m_dwCLUBID; __time64_t m_tStartTime; __time64_t m_tEndTime;
//   WORD m_wKillPoint; WORD m_wDeathPoint; bool m_bAlliance; char m_szClubName[33];

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
#include <algorithm>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <list>
#include <deque>
#include <queue>
#include <fstream>

#include <strsafe.h>
#include "d3dx9.h"

#include "s_NetGlobal.h"
#include "GLClubMan.h"
#include "GLContrlClubMsg.h"

#define L(f) ((int)offsetof(GLCLUBBATTLE_LOBY, f))

int main()
{
    printf("GLCLUBBATTLE_LOBY size=%d\n", (int)sizeof(GLCLUBBATTLE_LOBY));
    printf("CHAR_SZNAME=%d\n", (int)CHAR_SZNAME);
    printf("m_dwCLUBID=%d\n",    L(m_dwCLUBID));
    printf("m_tStartTime=%d\n",  L(m_tStartTime));
    printf("m_tEndTime=%d\n",    L(m_tEndTime));
    printf("m_wKillPoint=%d\n",  L(m_wKillPoint));
    printf("m_wDeathPoint=%d\n", L(m_wDeathPoint));
    printf("m_bAlliance=%d\n",   L(m_bAlliance));
    printf("m_szClubName=%d\n",  L(m_szClubName));

    // Where sBATTLE sits inside the lobby message, and the element stride, so an
    // absolute body offset for element i field f is:
    //   (sBATTLE off - 8 header) + i*stride + offsetof(GLCLUBBATTLE_LOBY, f)
    printf("SNETLOBBY_CLUB_BATTLE.sBATTLE=%d\n",
        (int)offsetof(GLMSG::SNETLOBBY_CLUB_BATTLE, sBATTLE));
    printf("SNETLOBBY_CLUB_BATTLE size=%d\n",
        (int)sizeof(GLMSG::SNETLOBBY_CLUB_BATTLE));
    printf("stride=%d\n",
        (int)((sizeof(GLMSG::SNETLOBBY_CLUB_BATTLE) - offsetof(GLMSG::SNETLOBBY_CLUB_BATTLE, sBATTLE)) / 10));
    return 0;
}
