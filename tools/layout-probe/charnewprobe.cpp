// charnewprobe — MSVC offsetof/sizeof for the character CREATE/DELETE wire
// structs (NET_NEW_CHA / NET_NEW_CHA_FB / NET_CHA_DEL / NET_CHA_DEL_FB).
//
// These live directly in s_NetGlobal.h — a DIFFERENT family from the
// GLContrl*Msg.h game-logic messages every other probe in this directory
// measures. s_NetGlobal.h has NO #pragma pack anywhere in it, so these are
// NATURAL alignment structs, and NET_NEW_CHA mixes WORDs with an int and a
// float — exactly the shape that hides padding (wSex is a WORD, fScaleRange
// a float needing 4-byte alignment) a hand count would either wrongly add or
// wrongly omit. Never guessed — measured.
//
// SOURCE/ is frozen — this only #includes it and prints ground truth.
// Build/run: charnewprobe.cmd
//
// Recorded output is pinned by RanCharCreatePackets and exercised by RunCheck.

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
#include <strsafe.h>
#include "d3dx9.h"

#include "s_NetGlobal.h"

#define OF(T, f) ((int)offsetof(T, f))

int main()
{
    printf("--- NET_MSG_GENERIC (s_NetGlobal.h, no pack pragma in this file) ---\n");
    printf("NET_MSG_GENERIC size=%d dwSize=%d nType=%d\n",
        (int)sizeof(NET_MSG_GENERIC), OF(NET_MSG_GENERIC, dwSize), OF(NET_MSG_GENERIC, nType));

    printf("--- NET_NEW_CHA (character CREATE request, client->agent) ---\n");
    printf("NET_NEW_CHA size=%d nIndex=%d wSchool=%d wFace=%d wHair=%d wHairColor=%d wSex=%d fScaleRange=%d szChaName=%d\n",
        (int)sizeof(NET_NEW_CHA), OF(NET_NEW_CHA, nIndex), OF(NET_NEW_CHA, wSchool),
        OF(NET_NEW_CHA, wFace), OF(NET_NEW_CHA, wHair), OF(NET_NEW_CHA, wHairColor),
        OF(NET_NEW_CHA, wSex), OF(NET_NEW_CHA, fScaleRange), OF(NET_NEW_CHA, szChaName));
    printf("CHR_ID_LENGTH=%d sizeof(szChaName)=%d\n",
        (int)CHR_ID_LENGTH, (int)sizeof(((NET_NEW_CHA*)0)->szChaName));

    printf("--- NET_NEW_CHA_FB (character CREATE reply, agent->client) ---\n");
    printf("NET_NEW_CHA_FB size=%d nResult=%d nChaNum=%d wChaRemain=%d nExtremeM=%d nExtremeW=%d\n",
        (int)sizeof(NET_NEW_CHA_FB), OF(NET_NEW_CHA_FB, nResult), OF(NET_NEW_CHA_FB, nChaNum),
        OF(NET_NEW_CHA_FB, wChaRemain), OF(NET_NEW_CHA_FB, nExtremeM), OF(NET_NEW_CHA_FB, nExtremeW));

    printf("--- EM_NEW_CHA_FB values ---\n");
    printf("EMNEWCHA sizeof enum=%d\n", (int)sizeof(EM_NEW_CHA_FB));

    return 0;
}
