// equipprobe — MSVC offsetof for the WORN-ITEM appearance fields the remote
// player-gear path needs (task: show worn gear on remote players/mobs).
//
// The parent's charprobe.cpp already measures SDROP_CHAR's scalar fields but
// stops before the equipment array. This adds the pieces needed to read another
// player's worn items off the DROP_PC (SDROP_CHAR) body:
//
//   SDROP_CHAR::m_PutOnItems[SLOT_NSIZE_S_2]  the worn item array
//   SDROP_CHAR::m_bUseArmSub                  main-vs-sub weapon-set selector
//   SITEMCLIENT { sNativeID, nidDISGUISE, ... } one worn item
//
// Same rule as every probe here: SOURCE/ is frozen — this only #includes it and
// prints ground truth. This is a NEW file; it does not touch charprobe.cpp.
//
// Build/run:  equipprobe.cmd

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
#include "GLDefine.h"
#include "GLItem.h"
#include "GLCharData.h"

#define OC(f) ((int)offsetof(SDROP_CHAR, f))
#define OI(f) ((int)offsetof(SITEMCLIENT, f))

int main()
{
    printf("SLOT_NSIZE_S_2=%d\n", (int)SLOT_NSIZE_S_2);
    printf("SDROP_CHAR size=%d\n", (int)sizeof(SDROP_CHAR));
    printf("SDROP_CHAR.m_PutOnItems=%d\n", OC(m_PutOnItems));
    printf("SDROP_CHAR.m_bUseArmSub=%d\n", OC(m_bUseArmSub));
    printf("SITEMCLIENT size=%d\n", (int)sizeof(SITEMCLIENT));
    printf("SITEMCLIENT.sNativeID=%d\n", OI(sNativeID));
    printf("SITEMCLIENT.nidDISGUISE=%d\n", OI(nidDISGUISE));
    // Appearance identity (2026-08-23, PC UpdateSuit parity): the face/hair
    // indices + hair colour every drop carries, and the per-item colour tints
    // SetColor1/SetColor2 apply — none were decoded on mobile before.
    printf("SDROP_CHAR.wHair=%d\n", OC(wHair));
    printf("SDROP_CHAR.wHairColor=%d\n", OC(wHairColor));
    printf("SDROP_CHAR.wFace=%d\n", OC(wFace));
    printf("SDROP_CHAR.wSex=%d\n", OC(wSex));
    printf("SITEMCLIENT.wColor1=%d\n", OI(wColor1));
    printf("SITEMCLIENT.wColor2=%d\n", OI(wColor2));
    // CHAR_JOIN's Data blob is SCHARDATA2 (SNETLOBBY_CHARJOIN.Data @ struct 53)
    // — the LOCAL player's identity, same trio the drop carries for remotes.
    printf("SCHARDATA2.m_emClass=%d\n", (int)offsetof(SCHARDATA2, m_emClass));
    printf("SCHARDATA2.m_wHair=%d\n", (int)offsetof(SCHARDATA2, m_wHair));
    printf("SCHARDATA2.m_wHairColor=%d\n", (int)offsetof(SCHARDATA2, m_wHairColor));
    printf("SCHARDATA2.m_wFace=%d\n", (int)offsetof(SCHARDATA2, m_wFace));
    printf("SCHARDATA2.m_PutOnItems=%d\n", (int)offsetof(SCHARDATA2, m_PutOnItems));
    printf("SNATIVEID size=%d\n", (int)sizeof(SNATIVEID));
    printf("SNATIVEID.wMainID=%d\n", (int)offsetof(SNATIVEID, wMainID));
    printf("SNATIVEID.wSubID=%d\n", (int)offsetof(SNATIVEID, wSubID));
    // EMSLOT anchors the reader iterates over.
    printf("SLOT_RHAND=%d SLOT_LHAND=%d SLOT_RHAND_S=%d SLOT_LHAND_S=%d SLOT_VEHICLE=%d\n",
           (int)SLOT_RHAND, (int)SLOT_LHAND, (int)SLOT_RHAND_S, (int)SLOT_LHAND_S, (int)SLOT_VEHICLE);
    return 0;
}
