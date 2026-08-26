// mallprobe — MSVC offsetof/sizeof for the ITEM MALL (cash shop) and CHARGED
// ITEM BANK wire structs in GLContrlInvenMsg.h.
//
// Two protocols the PC EP9 build carries in-game (measured, not summed):
//   * charged item bank  : m_cInvenCharged (CHARGED_INVEN 6x32), take-only, keyed
//     by a purchase key.   SNET_GET_CHARGEDITEM_FROMDB[_FB], SNET_CHARGED_ITEM_GET
//     [_FB], SNET_CHARGED_ITEM_DEL.
//   * reworked grid mall : the "ItemShop Reworked w/ Gift & Discount" EP1 port,
//     point-priced (PPoints / VPoints).   SNET_GET_ITEMSHOP_FROMDB[_FB],
//     SNET_ITEMSHOP_BUY[_FB], SNETPC_RETRIEVE_POINTS[_FB].
//
// These wrappers live in GLContrlInvenMsg.h, which is entirely #pragma pack(1)
// (line 301); the embedded ITEMSHOP is defined at natural alignment in
// s_NetGlobal.h and SNATIVEID is a 4-byte union — that mix is measured here,
// never guessed. Same rule as every probe: SOURCE/ is frozen, this only
// #includes it and prints ground truth.  Build/run:  mallprobe.cmd
//
// Recorded output is pinned by RanChargedBankPackets / RanItemMallPackets and
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
    printf("--- constants ---\n");
    printf("USR_ID_LENGTH=%d PURKEY_LENGTH=%d CHAR_SZNAME=%d NET_MSG_GENERIC=%d\n",
        (int)USR_ID_LENGTH, (int)PURKEY_LENGTH, (int)CHAR_SZNAME, (int)sizeof(NET_MSG_GENERIC));

    printf("--- core reused types ---\n");
    printf("SNATIVEID size=%d dwID=%d wMainID=%d wSubID=%d\n",
        (int)sizeof(SNATIVEID), OF(SNATIVEID, dwID), OF(SNATIVEID, wMainID), OF(SNATIVEID, wSubID));
    printf("ITEMSHOP size=%d sID=%d wItemPrice=%d wItemStock=%d wItemCtg=%d wItemCurrency=%d "
           "wItemVIPRank=%d wItemVIPType=%d wItemDiscount=%d strItemNum=%d\n",
        (int)sizeof(ITEMSHOP), OF(ITEMSHOP, sID), OF(ITEMSHOP, wItemPrice), OF(ITEMSHOP, wItemStock),
        OF(ITEMSHOP, wItemCtg), OF(ITEMSHOP, wItemCurrency), OF(ITEMSHOP, wItemVIPRank),
        OF(ITEMSHOP, wItemVIPType), OF(ITEMSHOP, wItemDiscount), OF(ITEMSHOP, strItemNum));

    printf("--- charged item bank (client<->server) ---\n");
    printf("SNET_GET_CHARGEDITEM_FROMDB size=%d dwCharID=%d szUID=%d\n",
        (int)sizeof(SNET_GET_CHARGEDITEM_FROMDB), OF(SNET_GET_CHARGEDITEM_FROMDB, dwCharID),
        OF(SNET_GET_CHARGEDITEM_FROMDB, szUID));
    printf("SNET_GET_CHARGEDITEM_FROMDB_FB size=%d emFB=%d szPurKey=%d nidITEM=%d\n",
        (int)sizeof(SNET_GET_CHARGEDITEM_FROMDB_FB), OF(SNET_GET_CHARGEDITEM_FROMDB_FB, emFB),
        OF(SNET_GET_CHARGEDITEM_FROMDB_FB, szPurKey), OF(SNET_GET_CHARGEDITEM_FROMDB_FB, nidITEM));
    printf("SNET_CHARGED_ITEM_GET size=%d dwID=%d szPurKey=%d\n",
        (int)sizeof(SNET_CHARGED_ITEM_GET), OF(SNET_CHARGED_ITEM_GET, dwID),
        OF(SNET_CHARGED_ITEM_GET, szPurKey));
    printf("SNET_CHARGED_ITEM_GET_FB size=%d emFB=%d nidITEM=%d\n",
        (int)sizeof(SNET_CHARGED_ITEM_GET_FB), OF(SNET_CHARGED_ITEM_GET_FB, emFB),
        OF(SNET_CHARGED_ITEM_GET_FB, nidITEM));
    printf("SNET_CHARGED_ITEM_DEL size=%d dwID=%d\n",
        (int)sizeof(SNET_CHARGED_ITEM_DEL), OF(SNET_CHARGED_ITEM_DEL, dwID));

    printf("--- item mall / reworked grid shop (client<->server) ---\n");
    printf("SNET_GET_ITEMSHOP_FROMDB size=%d dwCharID=%d szUID=%d\n",
        (int)sizeof(SNET_GET_ITEMSHOP_FROMDB), OF(SNET_GET_ITEMSHOP_FROMDB, dwCharID),
        OF(SNET_GET_ITEMSHOP_FROMDB, szUID));
    printf("SNET_GET_ITEMSHOP_FROMDB_FB size=%d emFB=%d nidITEM=%d wPrice=%d wStock=%d wCtg=%d "
           "wCurrency=%d wDiscount=%d sItemShop=%d szPurKey=%d\n",
        (int)sizeof(SNET_GET_ITEMSHOP_FROMDB_FB), OF(SNET_GET_ITEMSHOP_FROMDB_FB, emFB),
        OF(SNET_GET_ITEMSHOP_FROMDB_FB, nidITEM), OF(SNET_GET_ITEMSHOP_FROMDB_FB, wPrice),
        OF(SNET_GET_ITEMSHOP_FROMDB_FB, wStock), OF(SNET_GET_ITEMSHOP_FROMDB_FB, wCtg),
        OF(SNET_GET_ITEMSHOP_FROMDB_FB, wCurrency), OF(SNET_GET_ITEMSHOP_FROMDB_FB, wDiscount),
        OF(SNET_GET_ITEMSHOP_FROMDB_FB, sItemShop), OF(SNET_GET_ITEMSHOP_FROMDB_FB, szPurKey));
    printf("SNET_ITEMSHOP_BUY size=%d wPosX=%d wPosY=%d dwID=%d szPurKey=%d bGift=%d szUID=%d\n",
        (int)sizeof(SNET_ITEMSHOP_BUY), OF(SNET_ITEMSHOP_BUY, wPosX), OF(SNET_ITEMSHOP_BUY, wPosY),
        OF(SNET_ITEMSHOP_BUY, dwID), OF(SNET_ITEMSHOP_BUY, szPurKey), OF(SNET_ITEMSHOP_BUY, bGift),
        OF(SNET_ITEMSHOP_BUY, szUID));
    printf("SNET_ITEMSHOP_BUY_FB size=%d wPosX=%d wPosY=%d nID=%d emFB=%d szName=%d\n",
        (int)sizeof(SNET_ITEMSHOP_BUY_FB), OF(SNET_ITEMSHOP_BUY_FB, wPosX), OF(SNET_ITEMSHOP_BUY_FB, wPosY),
        OF(SNET_ITEMSHOP_BUY_FB, nID), OF(SNET_ITEMSHOP_BUY_FB, emFB), OF(SNET_ITEMSHOP_BUY_FB, szName));
    printf("SNETPC_RETRIEVE_POINTS size=%d\n", (int)sizeof(SNETPC_RETRIEVE_POINTS));
    printf("SNETPC_RETRIEVE_POINTS_FB size=%d emFB=%d PPoints=%d VPoints=%d\n",
        (int)sizeof(SNETPC_RETRIEVE_POINTS_FB), OF(SNETPC_RETRIEVE_POINTS_FB, emFB),
        OF(SNETPC_RETRIEVE_POINTS_FB, PPoints), OF(SNETPC_RETRIEVE_POINTS_FB, VPoints));
    return 0;
}
