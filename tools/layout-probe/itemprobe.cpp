// itemprobe — MSVC offsetof/sizeof for the inventory + equip wire structs.
//
// The main layout probe (gen-structs.js) measures the message *sizes* but not
// the interior offsets of SITEMCUSTOM / SINVENITEM, which are the strides the
// mobile inventory reader needs. Both message headers are under #pragma pack(1),
// but SITEMCUSTOM / SINVENITEM are DEFINED in GLItem.h at natural alignment, so
// their interior layout keeps its padding while their OFFSET inside a packed
// message does not — precisely the mix that must be measured, never summed.
//
// Same rule as the main probe: SOURCE/ is frozen — this only #includes it and
// prints ground truth.  Build/run:  itemprobe.cmd
//
// Recorded output is pinned by RanInventoryPackets / RanInventoryModule and
// exercised by RunCheck.

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
    printf("--- core strides (natural alignment, GLItem.h) ---\n");
    printf("SNATIVEID size=%d wMainID=%d wSubID=%d\n",
        (int)sizeof(SNATIVEID), OF(SNATIVEID, wMainID), OF(SNATIVEID, wSubID));
    printf("SITEMCUSTOM size=%d sNativeID=%d wTurnNum=%d cGenType=%d\n",
        (int)sizeof(SITEMCUSTOM), OF(SITEMCUSTOM, sNativeID),
        OF(SITEMCUSTOM, wTurnNum), OF(SITEMCUSTOM, cGenType));
    printf("SITEMCLIENT size=%d sNativeID=%d wTurnNum=%d\n",
        (int)sizeof(SITEMCLIENT), OF(SITEMCLIENT, sNativeID), OF(SITEMCLIENT, wTurnNum));
    printf("SINVENITEM size=%d wPosX=%d wPosY=%d sItemCustom=%d\n",
        (int)sizeof(SINVENITEM), OF(SINVENITEM, wPosX), OF(SINVENITEM, wPosY),
        OF(SINVENITEM, sItemCustom));

    printf("--- NET_MSG_GENERIC + enum sizes ---\n");
    printf("NET_MSG_GENERIC size=%d\n", (int)sizeof(NET_MSG_GENERIC));
    printf("EMSLOT size=%d  SLOT_TSIZE=%d\n", (int)sizeof(EMSLOT), (int)SLOT_TSIZE);

    printf("--- initial burst payloads (pack(1) messages) ---\n");
    printf("SNETLOBBY_INVENITEM size=%d Data=%d\n",
        (int)sizeof(SNETLOBBY_INVENITEM), OF(SNETLOBBY_INVENITEM, Data));
    printf("SNETLOBBY_CHARPUTON_EX size=%d nSlot=%d PutOnItem=%d\n",
        (int)sizeof(SNETLOBBY_CHARPUTON_EX), OF(SNETLOBBY_CHARPUTON_EX, nSlot),
        OF(SNETLOBBY_CHARPUTON_EX, PutOnItem));
    printf("SNETLOBBY_CHARPUTON size=%d PutOnItems=%d\n",
        (int)sizeof(SNETLOBBY_CHARPUTON), OF(SNETLOBBY_CHARPUTON, PutOnItems));

    printf("--- inventory update payloads (server->client) ---\n");
    printf("SNETPC_INVEN_INSERT size=%d Data=%d\n",
        (int)sizeof(SNETPC_INVEN_INSERT), OF(SNETPC_INVEN_INSERT, Data));
    printf("SNETPC_INVEN_DELETE size=%d wPosX=%d wPosY=%d\n",
        (int)sizeof(SNETPC_INVEN_DELETE), OF(SNETPC_INVEN_DELETE, wPosX),
        OF(SNETPC_INVEN_DELETE, wPosY));
    printf("SNETPC_INVEN_DEL_AND_INSERT size=%d wDelX=%d wDelY=%d sInsert=%d\n",
        (int)sizeof(SNETPC_INVEN_DEL_AND_INSERT), OF(SNETPC_INVEN_DEL_AND_INSERT, wDelX),
        OF(SNETPC_INVEN_DEL_AND_INSERT, wDelY), OF(SNETPC_INVEN_DEL_AND_INSERT, sInsert));
    printf("SNETPC_INVEN_DRUG_UPDATE size=%d wPosX=%d wPosY=%d wTurnNum=%d\n",
        (int)sizeof(SNETPC_INVEN_DRUG_UPDATE), OF(SNETPC_INVEN_DRUG_UPDATE, wPosX),
        OF(SNETPC_INVEN_DRUG_UPDATE, wPosY), OF(SNETPC_INVEN_DRUG_UPDATE, wTurnNum));
    printf("SNET_INVEN_ITEM_UPDATE size=%d wPosX=%d wPosY=%d sItemCustom=%d\n",
        (int)sizeof(SNET_INVEN_ITEM_UPDATE), OF(SNET_INVEN_ITEM_UPDATE, wPosX),
        OF(SNET_INVEN_ITEM_UPDATE, wPosY), OF(SNET_INVEN_ITEM_UPDATE, sItemCustom));

    printf("--- equip update payloads (server->client) ---\n");
    printf("SNETPC_PUTON_UPDATE size=%d emSlotRelease=%d emSlot=%d sItemCustom=%d\n",
        (int)sizeof(SNETPC_PUTON_UPDATE), OF(SNETPC_PUTON_UPDATE, emSlotRelease),
        OF(SNETPC_PUTON_UPDATE, emSlot), OF(SNETPC_PUTON_UPDATE, sItemCustom));
    printf("SNETPC_PUTON_RELEASE size=%d emSlot=%d bRefresh=%d\n",
        (int)sizeof(SNETPC_PUTON_RELEASE), OF(SNETPC_PUTON_RELEASE, emSlot),
        OF(SNETPC_PUTON_RELEASE, bRefresh));
    printf("SNETPC_PUTON_DRUG_UPDATE size=%d emSlot=%d wTurnNum=%d\n",
        (int)sizeof(SNETPC_PUTON_DRUG_UPDATE), OF(SNETPC_PUTON_DRUG_UPDATE, emSlot),
        OF(SNETPC_PUTON_DRUG_UPDATE, wTurnNum));

    printf("--- equip/use request payloads (client->server) ---\n");
    printf("SNETPC_REQ_INVEN_TO_HOLD size=%d wPosX=%d wPosY=%d\n",
        (int)sizeof(SNETPC_REQ_INVEN_TO_HOLD), OF(SNETPC_REQ_INVEN_TO_HOLD, wPosX),
        OF(SNETPC_REQ_INVEN_TO_HOLD, wPosY));
    printf("SNETPC_REQ_INVEN_EX_HOLD size=%d wPosX=%d wPosY=%d\n",
        (int)sizeof(SNETPC_REQ_INVEN_EX_HOLD), OF(SNETPC_REQ_INVEN_EX_HOLD, wPosX),
        OF(SNETPC_REQ_INVEN_EX_HOLD, wPosY));
    printf("SNETPC_REQ_INVEN_TO_SLOT size=%d wPosX=%d wPosY=%d emToSlot=%d\n",
        (int)sizeof(SNETPC_REQ_INVEN_TO_SLOT), OF(SNETPC_REQ_INVEN_TO_SLOT, wPosX),
        OF(SNETPC_REQ_INVEN_TO_SLOT, wPosY), OF(SNETPC_REQ_INVEN_TO_SLOT, emToSlot));
    printf("SNETPC_REQ_SLOT_TO_HOLD size=%d emSlot=%d\n",
        (int)sizeof(SNETPC_REQ_SLOT_TO_HOLD), OF(SNETPC_REQ_SLOT_TO_HOLD, emSlot));
    printf("SNETPC_REQ_SLOT_EX_HOLD size=%d emSlot=%d\n",
        (int)sizeof(SNETPC_REQ_SLOT_EX_HOLD), OF(SNETPC_REQ_SLOT_EX_HOLD, emSlot));
    printf("SNETPC_REQ_HOLD_TO_SLOT size=%d emSlot=%d\n",
        (int)sizeof(SNETPC_REQ_HOLD_TO_SLOT), OF(SNETPC_REQ_HOLD_TO_SLOT, emSlot));
    printf("SNETPC_REQ_HOLD_TO_INVEN size=%d wPosX=%d wPosY=%d bUseVietnamInven=%d\n",
        (int)sizeof(SNETPC_REQ_HOLD_TO_INVEN), OF(SNETPC_REQ_HOLD_TO_INVEN, wPosX),
        OF(SNETPC_REQ_HOLD_TO_INVEN, wPosY), OF(SNETPC_REQ_HOLD_TO_INVEN, bUseVietnamInven));
    printf("SNETPC_REQ_INVENDRUG size=%d wPosX=%d wPosY=%d\n",
        (int)sizeof(SNETPC_REQ_INVENDRUG), OF(SNETPC_REQ_INVENDRUG, wPosX),
        OF(SNETPC_REQ_INVENDRUG, wPosY));
    return 0;
}
