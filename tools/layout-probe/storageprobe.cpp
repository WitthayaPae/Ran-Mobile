// storageprobe — MSVC offsetof/sizeof for the personal ITEM-STORAGE (bank) wire
// structs in GLContrlInvenMsg.h.
//
// The item-storage protocol (m_cStorage[channel] + m_lnStorageMoney, opened at a
// bank NPC — the PC MENU_ITEMBANK_BUTTON) reuses SINVENITEM / SITEMCUSTOM but
// wraps them in its own message set. Like the inventory messages these wrappers
// are under #pragma pack(1) (GLContrlInvenMsg.h line 301), while SINVENITEM /
// SITEMCUSTOM are DEFINED in GLItem.h at natural alignment — so a wrapper that
// carries an item keeps the item's interior padding but not any padding before
// it. That mix is measured here, never summed, exactly like itemprobe.
//
// Same rule as every probe: SOURCE/ is frozen — this only #includes it and
// prints ground truth.  Build/run:  storageprobe.cmd
//
// Recorded output is pinned by RanStoragePackets and exercised by RunCheck.

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
#include "GLContrlPcMsg.h"
#include "GLContrlCharJoinMsg.h"
#include "GLContrlInvenMsg.h"

using namespace GLMSG;

#define OF(T, f) ((int)offsetof(T, f))

int main()
{
    printf("--- core strides reused by storage (natural alignment, GLItem.h) ---\n");
    printf("SITEMCUSTOM size=%d sNativeID=%d wTurnNum=%d\n",
        (int)sizeof(SITEMCUSTOM), OF(SITEMCUSTOM, sNativeID), OF(SITEMCUSTOM, wTurnNum));
    printf("SINVENITEM size=%d wPosX=%d wPosY=%d sItemCustom=%d\n",
        (int)sizeof(SINVENITEM), OF(SINVENITEM, wPosX), OF(SINVENITEM, wPosY),
        OF(SINVENITEM, sItemCustom));
    printf("NET_MSG_GENERIC size=%d\n", (int)sizeof(NET_MSG_GENERIC));

    printf("--- storage requests (client->server) ---\n");
    printf("SNETPC_REQ_GETSTORAGE size=%d dwChannel=%d dwNPCID=%d\n",
        (int)sizeof(SNETPC_REQ_GETSTORAGE), OF(SNETPC_REQ_GETSTORAGE, dwChannel),
        OF(SNETPC_REQ_GETSTORAGE, dwNPCID));
    printf("SNETPC_REQ_STORAGE_TO_HOLD size=%d dwChannel=%d dwNPCID=%d wPosX=%d wPosY=%d\n",
        (int)sizeof(SNETPC_REQ_STORAGE_TO_HOLD), OF(SNETPC_REQ_STORAGE_TO_HOLD, dwChannel),
        OF(SNETPC_REQ_STORAGE_TO_HOLD, dwNPCID), OF(SNETPC_REQ_STORAGE_TO_HOLD, wPosX),
        OF(SNETPC_REQ_STORAGE_TO_HOLD, wPosY));
    printf("SNETPC_REQ_STORAGE_EX_HOLD size=%d dwChannel=%d dwNPCID=%d wPosX=%d wPosY=%d\n",
        (int)sizeof(SNETPC_REQ_STORAGE_EX_HOLD), OF(SNETPC_REQ_STORAGE_EX_HOLD, dwChannel),
        OF(SNETPC_REQ_STORAGE_EX_HOLD, dwNPCID), OF(SNETPC_REQ_STORAGE_EX_HOLD, wPosX),
        OF(SNETPC_REQ_STORAGE_EX_HOLD, wPosY));
    printf("SNETPC_REQ_HOLD_TO_STORAGE size=%d dwChannel=%d dwNPCID=%d wPosX=%d wPosY=%d\n",
        (int)sizeof(SNETPC_REQ_HOLD_TO_STORAGE), OF(SNETPC_REQ_HOLD_TO_STORAGE, dwChannel),
        OF(SNETPC_REQ_HOLD_TO_STORAGE, dwNPCID), OF(SNETPC_REQ_HOLD_TO_STORAGE, wPosX),
        OF(SNETPC_REQ_HOLD_TO_STORAGE, wPosY));
    printf("SNETPC_REQ_STORAGEDRUG size=%d dwChannel=%d dwNPCID=%d wPosX=%d wPosY=%d\n",
        (int)sizeof(SNETPC_REQ_STORAGEDRUG), OF(SNETPC_REQ_STORAGEDRUG, dwChannel),
        OF(SNETPC_REQ_STORAGEDRUG, dwNPCID), OF(SNETPC_REQ_STORAGEDRUG, wPosX),
        OF(SNETPC_REQ_STORAGEDRUG, wPosY));
    printf("SNETPC_REQ_STORAGE_SPLIT size=%d dwChannel=%d dwNPCID=%d wPosX=%d wPosY=%d wSplit=%d\n",
        (int)sizeof(SNETPC_REQ_STORAGE_SPLIT), OF(SNETPC_REQ_STORAGE_SPLIT, dwChannel),
        OF(SNETPC_REQ_STORAGE_SPLIT, dwNPCID), OF(SNETPC_REQ_STORAGE_SPLIT, wPosX),
        OF(SNETPC_REQ_STORAGE_SPLIT, wPosY), OF(SNETPC_REQ_STORAGE_SPLIT, wSplit));
    printf("SNETPC_REQ_STORAGE_SAVE_MONEY size=%d lnMoney=%d dwNPCID=%d\n",
        (int)sizeof(SNETPC_REQ_STORAGE_SAVE_MONEY), OF(SNETPC_REQ_STORAGE_SAVE_MONEY, lnMoney),
        OF(SNETPC_REQ_STORAGE_SAVE_MONEY, dwNPCID));
    printf("SNETPC_REQ_STORAGE_DRAW_MONEY size=%d lnMoney=%d dwNPCID=%d\n",
        (int)sizeof(SNETPC_REQ_STORAGE_DRAW_MONEY), OF(SNETPC_REQ_STORAGE_DRAW_MONEY, lnMoney),
        OF(SNETPC_REQ_STORAGE_DRAW_MONEY, dwNPCID));

    printf("--- storage feedback / contents (server->client) ---\n");
    printf("SNETPC_REQ_GETSTORAGE_FB size=%d lnMoney=%d dwChannel=%d dwNumStorageItem=%d\n",
        (int)sizeof(SNETPC_REQ_GETSTORAGE_FB), OF(SNETPC_REQ_GETSTORAGE_FB, lnMoney),
        OF(SNETPC_REQ_GETSTORAGE_FB, dwChannel), OF(SNETPC_REQ_GETSTORAGE_FB, dwNumStorageItem));
    printf("SNETPC_REQ_GETSTORAGE_ITEM size=%d dwChannel=%d Data=%d\n",
        (int)sizeof(SNETPC_REQ_GETSTORAGE_ITEM), OF(SNETPC_REQ_GETSTORAGE_ITEM, dwChannel),
        OF(SNETPC_REQ_GETSTORAGE_ITEM, Data));
    printf("SNETPC_STORAGE_INSERT size=%d dwChannel=%d Data=%d\n",
        (int)sizeof(SNETPC_STORAGE_INSERT), OF(SNETPC_STORAGE_INSERT, dwChannel),
        OF(SNETPC_STORAGE_INSERT, Data));
    printf("SNETPC_STORAGE_DELETE size=%d dwChannel=%d wPosX=%d wPosY=%d\n",
        (int)sizeof(SNETPC_STORAGE_DELETE), OF(SNETPC_STORAGE_DELETE, dwChannel),
        OF(SNETPC_STORAGE_DELETE, wPosX), OF(SNETPC_STORAGE_DELETE, wPosY));
    printf("SNETPC_STORAGE_ITEM_UPDATE size=%d dwChannel=%d wPosX=%d wPosY=%d sItemCustom=%d\n",
        (int)sizeof(SNETPC_STORAGE_ITEM_UPDATE), OF(SNETPC_STORAGE_ITEM_UPDATE, dwChannel),
        OF(SNETPC_STORAGE_ITEM_UPDATE, wPosX), OF(SNETPC_STORAGE_ITEM_UPDATE, wPosY),
        OF(SNETPC_STORAGE_ITEM_UPDATE, sItemCustom));
    printf("SNETPC_STORAGE_DEL_AND_INSERT size=%d dwChannel=%d wDelX=%d wDelY=%d sInsert=%d\n",
        (int)sizeof(SNETPC_STORAGE_DEL_AND_INSERT), OF(SNETPC_STORAGE_DEL_AND_INSERT, dwChannel),
        OF(SNETPC_STORAGE_DEL_AND_INSERT, wDelX), OF(SNETPC_STORAGE_DEL_AND_INSERT, wDelY),
        OF(SNETPC_STORAGE_DEL_AND_INSERT, sInsert));
    printf("SNETPC_STORAGE_DRUG_UPDATE size=%d dwChannel=%d wPosX=%d wPosY=%d wTurnNum=%d\n",
        (int)sizeof(SNETPC_STORAGE_DRUG_UPDATE), OF(SNETPC_STORAGE_DRUG_UPDATE, dwChannel),
        OF(SNETPC_STORAGE_DRUG_UPDATE, wPosX), OF(SNETPC_STORAGE_DRUG_UPDATE, wPosY),
        OF(SNETPC_STORAGE_DRUG_UPDATE, wTurnNum));
    printf("SNETPC_REQ_STORAGE_UPDATE_MONEY size=%d lnMoney=%d\n",
        (int)sizeof(SNETPC_REQ_STORAGE_UPDATE_MONEY), OF(SNETPC_REQ_STORAGE_UPDATE_MONEY, lnMoney));
    return 0;
}
