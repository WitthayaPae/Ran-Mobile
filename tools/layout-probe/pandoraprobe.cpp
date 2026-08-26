// pandoraprobe — MSVC offsetof/sizeof for the PANDORA BOX wire structs.
//
// GLPANDORA_BOX / GLPANDORA_BOX_STATUS / GLPANDORA_BOX_SETTING (GLCharData.h)
// are defined with NO #pragma pack of their own, so their interior layout is
// fixed at whatever alignment is active when GLCharData.h is first included —
// the same trap already confirmed for SDROPPET (petprobe) and SCHARDATA
// (charprobe). They are then embedded inside GLContrlPcMsg.h's SNET_PANDORA_*
// message wrappers, which DO open #pragma pack(1) at line 356 (closed again at
// line 4449) — but that only controls how the WRAPPER packs its own members,
// not the interior of an already-fixed-size embedded type.
//
// GLPANDORA_BOX is also blitted with ReadBuffer/WriteBuffer directly
// (GLCharDataPandora.cpp SCHARDATA2::GETPANDORA_BYBUF/SETPANDORA_BYBUF), so
// sizeof(GLPANDORA_BOX) is a real stride, not just a message-body detail.
//
// Include order mirrors charprobe.cpp (already proven to compile), which is
// the closest real precedent for this exact header pair: GLContrlPcMsg.h
// (defines the message wrappers, opens/closes its own pack(1)) then
// GLContrlMsg.h / GLContrlCrowMsg.h / GLCrowData.h then GLCharData.h (defines
// GLPANDORA_BOX at natural alignment, same conclusion charprobe.cpp already
// reached for SCHARDATA: pack(1) is properly closed before GLCharData.h is
// ever parsed, in every order this build actually uses).
//
// SOURCE/ is frozen — this only #includes it and prints ground truth.
// Build/run: pandoraprobe.cmd

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

using namespace GLMSG;

#define OF(T, f) ((int)offsetof(T, f))

int main()
{
    printf("--- GLPANDORA_BOX / _STATUS / _SETTING (GLCharData.h, alignment at include point) ---\n");
    printf("GLPANDORA_BOX size=%d\n", (int)sizeof(GLPANDORA_BOX));
    printf("  nIndex=%d sItemID=%d llGold=%d wEP=%d fRate=%d\n",
        OF(GLPANDORA_BOX, nIndex), OF(GLPANDORA_BOX, sItemID), OF(GLPANDORA_BOX, llGold),
        OF(GLPANDORA_BOX, wEP), OF(GLPANDORA_BOX, fRate));
    printf("GLPANDORA_BOX::PANDORA_RESULT_SIZE=%d\n", (int)GLPANDORA_BOX::PANDORA_RESULT_SIZE);

    printf("GLPANDORA_BOX_STATUS size=%d\n", (int)sizeof(GLPANDORA_BOX_STATUS));
    printf("  uCount=%d fReqDelay=%d emStatus=%d\n",
        OF(GLPANDORA_BOX_STATUS, uCount), OF(GLPANDORA_BOX_STATUS, fReqDelay), OF(GLPANDORA_BOX_STATUS, emStatus));

    printf("GLPANDORA_BOX_SETTING size=%d\n", (int)sizeof(GLPANDORA_BOX_SETTING));
    printf("  llGold=%d wEP=%d fPremChance=%d fReqDelay=%d\n",
        OF(GLPANDORA_BOX_SETTING, llGold), OF(GLPANDORA_BOX_SETTING, wEP),
        OF(GLPANDORA_BOX_SETTING, fPremChance), OF(GLPANDORA_BOX_SETTING, fReqDelay));

    printf("--- SNET_PANDORA_BOX_* (GLContrlPcMsg.h, pack(1) region) ---\n");
    printf("NET_MSG_GENERIC size=%d\n", (int)sizeof(NET_MSG_GENERIC));

    printf("SNET_PANDORA_BOX_OPEN_REQ size=%d emReq=%d\n",
        (int)sizeof(SNET_PANDORA_BOX_OPEN_REQ), OF(SNET_PANDORA_BOX_OPEN_REQ, emReq));
    printf("SNET_PANDORA_BOX_OPEN_FB size=%d emFB=%d\n",
        (int)sizeof(SNET_PANDORA_BOX_OPEN_FB), OF(SNET_PANDORA_BOX_OPEN_FB, emFB));

    printf("SNET_PANDORA_BOX_RESULT size=%d sBOX=%d sStatus=%d\n",
        (int)sizeof(SNET_PANDORA_BOX_RESULT), OF(SNET_PANDORA_BOX_RESULT, sBOX), OF(SNET_PANDORA_BOX_RESULT, sStatus));

    printf("SNET_PANDORA_BOX_BUY_ITEM size=%d emReq=%d nIndex=%d\n",
        (int)sizeof(SNET_PANDORA_BOX_BUY_ITEM), OF(SNET_PANDORA_BOX_BUY_ITEM, emReq), OF(SNET_PANDORA_BOX_BUY_ITEM, nIndex));
    printf("SNET_PANDORA_BOX_BUY_ITEM_FB size=%d emFB=%d\n",
        (int)sizeof(SNET_PANDORA_BOX_BUY_ITEM_FB), OF(SNET_PANDORA_BOX_BUY_ITEM_FB, emFB));

    printf("SNET_PANDORA_BOX_CLEAR_RESULTS_REQ size=%d\n", (int)sizeof(SNET_PANDORA_BOX_CLEAR_RESULTS_REQ));
    printf("SNET_PANDORA_BOX_CLEAR_RESULTS_FB size=%d emFB=%d\n",
        (int)sizeof(SNET_PANDORA_BOX_CLEAR_RESULTS_FB), OF(SNET_PANDORA_BOX_CLEAR_RESULTS_FB, emFB));

    printf("SNET_PANDORA_BOX_REFRESH_RESULT size=%d\n", (int)sizeof(SNET_PANDORA_BOX_REFRESH_RESULT));

    printf("SNET_PANDORA_BOX_SETTING_INFO size=%d llGold=%d wEP=%d fPremChance=%d fReqDelay=%d\n",
        (int)sizeof(SNET_PANDORA_BOX_SETTING_INFO), OF(SNET_PANDORA_BOX_SETTING_INFO, llGold),
        OF(SNET_PANDORA_BOX_SETTING_INFO, wEP), OF(SNET_PANDORA_BOX_SETTING_INFO, fPremChance),
        OF(SNET_PANDORA_BOX_SETTING_INFO, fReqDelay));

    printf("SNET_PANDORA_BOX_REWARD_RARE_BRD size=%d szChaName=%d sBOX=%d\n",
        (int)sizeof(SNET_PANDORA_BOX_REWARD_RARE_BRD), OF(SNET_PANDORA_BOX_REWARD_RARE_BRD, szChaName),
        OF(SNET_PANDORA_BOX_REWARD_RARE_BRD, sBOX));

    return 0;
}
