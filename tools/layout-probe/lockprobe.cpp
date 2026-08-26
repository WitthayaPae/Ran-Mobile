// lockprobe — MSVC offsetof/sizeof (and resolved opcode values) for the
// SECURITY/LOCK wire structs in GLContrlPcMsg2.h (equipment / storage /
// inventory PIN-lock ENABLE / INPUT / RECOVER / RECOVER_CHANGE /
// RECOVER_DELETE families). All three families are pack(1) and every field
// is either a char[] or a 4-byte type (DWORD-sized enum or BOOL) — no WORD
// fields are interleaved, so there is no natural-alignment padding to strip
// even outside pack(1) — but this is MEASURED here anyway, never summed by
// hand, per project convention (storageprobe.cpp / itemprobe.cpp).
//
// Also resolves the NET_MSG_GCTRL_* opcode enum members to their final
// integer values directly from the compiler, instead of hand-adding the
// "(NET_MSG_GCTRL + N)" source literals — NET_MSG_GCTRL itself has multiple
// candidate #defines guarded by build config in s_NetGlobal.h, so only the
// compiler (which resolves the one actually active) is authoritative.
//
// Same rule as every probe: SOURCE/ is frozen — this only #includes it and
// prints ground truth. Build/run: lockprobe.cmd
//
// Recorded output is pinned by RanLockPackets and exercised by RunCheck.

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
#include "GLContrlPcMsg2.h"

using namespace GLMSG;

#define OF(T, f) ((int)offsetof(T, f))

int main()
{
    printf("--- pass sizes ---\n");
    printf("CHAR_EQUIPMENT_LOCK_PASS_SIZE=%d USER_STORAGE_LOCK_PASS_SIZE=%d CHAR_INVENTORY_LOCK_PASS_SIZE=%d CHAR_SZNAME=%d\n",
        (int)CHAR_EQUIPMENT_LOCK_PASS_SIZE, (int)USER_STORAGE_LOCK_PASS_SIZE,
        (int)CHAR_INVENTORY_LOCK_PASS_SIZE, (int)CHAR_SZNAME);
    printf("NET_MSG_GENERIC size=%d NET_MSG_GCTRL=%d\n", (int)sizeof(NET_MSG_GENERIC), (int)NET_MSG_GCTRL);

    printf("--- opcodes: EQUIPMENT (resolved ints) ---\n");
    printf("ENABLE=%d ENABLE_FB=%d ENABLE_FROM_DB=%d\n",
        (int)NET_MSG_GCTRL_INVEN_EQUIPMENT_LOCK_ENABLE, (int)NET_MSG_GCTRL_INVEN_EQUIPMENT_LOCK_ENABLE_FB,
        (int)NET_MSG_GCTRL_INVEN_EQUIPMENT_LOCK_ENABLE_FROM_DB);
    printf("INPUT=%d INPUT_FB=%d INPUT_FROM_DB=%d\n",
        (int)NET_MSG_GCTRL_INVEN_EQUIPMENT_LOCK_INPUT, (int)NET_MSG_GCTRL_INVEN_EQUIPMENT_LOCK_INPUT_FB,
        (int)NET_MSG_GCTRL_INVEN_EQUIPMENT_LOCK_INPUT_FROM_DB);
    printf("RECOVER=%d RECOVER_FB=%d RECOVER_FROM_DB=%d\n",
        (int)NET_MSG_GCTRL_INVEN_EQUIPMENT_LOCK_RECOVER, (int)NET_MSG_GCTRL_INVEN_EQUIPMENT_LOCK_RECOVER_FB,
        (int)NET_MSG_GCTRL_INVEN_EQUIPMENT_LOCK_RECOVER_FROM_DB);
    printf("RECOVER_CHANGE=%d RECOVER_CHANGE_FB=%d RECOVER_CHANGE_FROM_DB=%d\n",
        (int)NET_MSG_GCTRL_INVEN_EQUIPMENT_LOCK_RECOVER_CHANGE, (int)NET_MSG_GCTRL_INVEN_EQUIPMENT_LOCK_RECOVER_CHANGE_FB,
        (int)NET_MSG_GCTRL_INVEN_EQUIPMENT_LOCK_RECOVER_CHANGE_FROM_DB);
    printf("RECOVER_DELETE=%d RECOVER_DELETE_FB=%d RECOVER_DELETE_FROM_DB=%d\n",
        (int)NET_MSG_GCTRL_INVEN_EQUIPMENT_LOCK_RECOVER_DELETE, (int)NET_MSG_GCTRL_INVEN_EQUIPMENT_LOCK_RECOVER_DELETE_FB,
        (int)NET_MSG_GCTRL_INVEN_EQUIPMENT_LOCK_RECOVER_DELETE_FROM_DB);

    printf("--- opcodes: STORAGE (resolved ints) ---\n");
    printf("ENABLE=%d ENABLE_FB=%d ENABLE_FROM_DB=%d\n",
        (int)NET_MSG_GCTRL_STORAGE_LOCK_ENABLE, (int)NET_MSG_GCTRL_STORAGE_LOCK_ENABLE_FB,
        (int)NET_MSG_GCTRL_STORAGE_LOCK_ENABLE_FROM_DB);
    printf("INPUT=%d INPUT_FB=%d INPUT_FROM_DB=%d\n",
        (int)NET_MSG_GCTRL_STORAGE_LOCK_INPUT, (int)NET_MSG_GCTRL_STORAGE_LOCK_INPUT_FB,
        (int)NET_MSG_GCTRL_STORAGE_LOCK_INPUT_FROM_DB);
    printf("RECOVER=%d RECOVER_FB=%d RECOVER_FROM_DB=%d\n",
        (int)NET_MSG_GCTRL_STORAGE_LOCK_RECOVER, (int)NET_MSG_GCTRL_STORAGE_LOCK_RECOVER_FB,
        (int)NET_MSG_GCTRL_STORAGE_LOCK_RECOVER_FROM_DB);
    printf("RECOVER_CHANGE=%d RECOVER_CHANGE_FB=%d RECOVER_CHANGE_FROM_DB=%d\n",
        (int)NET_MSG_GCTRL_STORAGE_LOCK_RECOVER_CHANGE, (int)NET_MSG_GCTRL_STORAGE_LOCK_RECOVER_CHANGE_FB,
        (int)NET_MSG_GCTRL_STORAGE_LOCK_RECOVER_CHANGE_FROM_DB);
    printf("RECOVER_DELETE=%d RECOVER_DELETE_FB=%d RECOVER_DELETE_FROM_DB=%d\n",
        (int)NET_MSG_GCTRL_STORAGE_LOCK_RECOVER_DELETE, (int)NET_MSG_GCTRL_STORAGE_LOCK_RECOVER_DELETE_FB,
        (int)NET_MSG_GCTRL_STORAGE_LOCK_RECOVER_DELETE_FROM_DB);

    printf("--- opcodes: INVENTORY (resolved ints) ---\n");
    printf("ENABLE=%d ENABLE_FB=%d ENABLE_FROM_DB=%d\n",
        (int)NET_MSG_GCTRL_INVENTORY_LOCK_ENABLE, (int)NET_MSG_GCTRL_INVENTORY_LOCK_ENABLE_FB,
        (int)NET_MSG_GCTRL_INVENTORY_LOCK_ENABLE_FROM_DB);
    printf("INPUT=%d INPUT_FB=%d INPUT_FROM_DB=%d\n",
        (int)NET_MSG_GCTRL_INVENTORY_LOCK_INPUT, (int)NET_MSG_GCTRL_INVENTORY_LOCK_INPUT_FB,
        (int)NET_MSG_GCTRL_INVENTORY_LOCK_INPUT_FROM_DB);
    printf("RECOVER=%d RECOVER_FB=%d RECOVER_FROM_DB=%d\n",
        (int)NET_MSG_GCTRL_INVENTORY_LOCK_RECOVER, (int)NET_MSG_GCTRL_INVENTORY_LOCK_RECOVER_FB,
        (int)NET_MSG_GCTRL_INVENTORY_LOCK_RECOVER_FROM_DB);
    printf("RECOVER_CHANGE=%d RECOVER_CHANGE_FB=%d RECOVER_CHANGE_FROM_DB=%d\n",
        (int)NET_MSG_GCTRL_INVENTORY_LOCK_RECOVER_CHANGE, (int)NET_MSG_GCTRL_INVENTORY_LOCK_RECOVER_CHANGE_FB,
        (int)NET_MSG_GCTRL_INVENTORY_LOCK_RECOVER_CHANGE_FROM_DB);
    printf("RECOVER_DELETE=%d RECOVER_DELETE_FB=%d RECOVER_DELETE_FROM_DB=%d\n",
        (int)NET_MSG_GCTRL_INVENTORY_LOCK_RECOVER_DELETE, (int)NET_MSG_GCTRL_INVENTORY_LOCK_RECOVER_DELETE_FB,
        (int)NET_MSG_GCTRL_INVENTORY_LOCK_RECOVER_DELETE_FROM_DB);

    printf("--- struct layout: EQUIPMENT ---\n");
    printf("ENABLE size=%d szPin1=%d szPin2=%d\n",
        (int)sizeof(SNETPC_INVEN_EQUIPMENT_LOCK_ENABLE), OF(SNETPC_INVEN_EQUIPMENT_LOCK_ENABLE, szPin1),
        OF(SNETPC_INVEN_EQUIPMENT_LOCK_ENABLE, szPin2));
    printf("ENABLE_FB size=%d emFB=%d bEquipmentLockEnable=%d\n",
        (int)sizeof(SNETPC_INVEN_EQUIPMENT_LOCK_ENABLE_FB), OF(SNETPC_INVEN_EQUIPMENT_LOCK_ENABLE_FB, emFB),
        OF(SNETPC_INVEN_EQUIPMENT_LOCK_ENABLE_FB, bEquipmentLockEnable));
    printf("INPUT size=%d szPin=%d\n",
        (int)sizeof(SNETPC_INVEN_EQUIPMENT_LOCK_INPUT), OF(SNETPC_INVEN_EQUIPMENT_LOCK_INPUT, szPin));
    printf("INPUT_FB size=%d emFB=%d bEquipmentLockStatus=%d\n",
        (int)sizeof(SNETPC_INVEN_EQUIPMENT_LOCK_INPUT_FB), OF(SNETPC_INVEN_EQUIPMENT_LOCK_INPUT_FB, emFB),
        OF(SNETPC_INVEN_EQUIPMENT_LOCK_INPUT_FB, bEquipmentLockStatus));
    printf("RECOVER size=%d\n", (int)sizeof(SNETPC_INVEN_EQUIPMENT_LOCK_RECOVER));
    printf("RECOVER_FB size=%d emFB=%d szName=%d szPin=%d\n",
        (int)sizeof(SNETPC_INVEN_EQUIPMENT_LOCK_RECOVER_FB), OF(SNETPC_INVEN_EQUIPMENT_LOCK_RECOVER_FB, emFB),
        OF(SNETPC_INVEN_EQUIPMENT_LOCK_RECOVER_FB, szName), OF(SNETPC_INVEN_EQUIPMENT_LOCK_RECOVER_FB, szPin));
    printf("RECOVER_CHANGE size=%d szPin1=%d szPin2=%d\n",
        (int)sizeof(SNETPC_INVEN_EQUIPMENT_LOCK_RECOVER_CHANGE), OF(SNETPC_INVEN_EQUIPMENT_LOCK_RECOVER_CHANGE, szPin1),
        OF(SNETPC_INVEN_EQUIPMENT_LOCK_RECOVER_CHANGE, szPin2));
    printf("RECOVER_CHANGE_FB size=%d emFB=%d\n",
        (int)sizeof(SNETPC_INVEN_EQUIPMENT_LOCK_RECOVER_CHANGE_FB), OF(SNETPC_INVEN_EQUIPMENT_LOCK_RECOVER_CHANGE_FB, emFB));
    printf("RECOVER_DELETE size=%d\n", (int)sizeof(SNETPC_INVEN_EQUIPMENT_LOCK_RECOVER_DELETE));
    printf("RECOVER_DELETE_FB size=%d emFB=%d bEquipmentLockEnable=%d bEquipmentLockStatus=%d\n",
        (int)sizeof(SNETPC_INVEN_EQUIPMENT_LOCK_RECOVER_DELETE_FB), OF(SNETPC_INVEN_EQUIPMENT_LOCK_RECOVER_DELETE_FB, emFB),
        OF(SNETPC_INVEN_EQUIPMENT_LOCK_RECOVER_DELETE_FB, bEquipmentLockEnable),
        OF(SNETPC_INVEN_EQUIPMENT_LOCK_RECOVER_DELETE_FB, bEquipmentLockStatus));

    printf("--- struct layout: STORAGE ---\n");
    printf("ENABLE size=%d szPin1=%d szPin2=%d\n",
        (int)sizeof(SNETPC_STORAGE_LOCK_ENABLE), OF(SNETPC_STORAGE_LOCK_ENABLE, szPin1),
        OF(SNETPC_STORAGE_LOCK_ENABLE, szPin2));
    printf("ENABLE_FB size=%d emFB=%d bStorageLockEnable=%d\n",
        (int)sizeof(SNETPC_STORAGE_LOCK_ENABLE_FB), OF(SNETPC_STORAGE_LOCK_ENABLE_FB, emFB),
        OF(SNETPC_STORAGE_LOCK_ENABLE_FB, bStorageLockEnable));
    printf("INPUT size=%d szPin=%d\n",
        (int)sizeof(SNETPC_STORAGE_LOCK_INPUT), OF(SNETPC_STORAGE_LOCK_INPUT, szPin));
    printf("INPUT_FB size=%d emFB=%d bStorageLockStatus=%d\n",
        (int)sizeof(SNETPC_STORAGE_LOCK_INPUT_FB), OF(SNETPC_STORAGE_LOCK_INPUT_FB, emFB),
        OF(SNETPC_STORAGE_LOCK_INPUT_FB, bStorageLockStatus));
    printf("RECOVER size=%d\n", (int)sizeof(SNETPC_STORAGE_LOCK_RECOVER));
    printf("RECOVER_FB size=%d emFB=%d szName=%d szPin=%d\n",
        (int)sizeof(SNETPC_STORAGE_LOCK_RECOVER_FB), OF(SNETPC_STORAGE_LOCK_RECOVER_FB, emFB),
        OF(SNETPC_STORAGE_LOCK_RECOVER_FB, szName), OF(SNETPC_STORAGE_LOCK_RECOVER_FB, szPin));
    printf("RECOVER_CHANGE size=%d szPin1=%d szPin2=%d\n",
        (int)sizeof(SNETPC_STORAGE_LOCK_RECOVER_CHANGE), OF(SNETPC_STORAGE_LOCK_RECOVER_CHANGE, szPin1),
        OF(SNETPC_STORAGE_LOCK_RECOVER_CHANGE, szPin2));
    printf("RECOVER_CHANGE_FB size=%d emFB=%d\n",
        (int)sizeof(SNETPC_STORAGE_LOCK_RECOVER_CHANGE_FB), OF(SNETPC_STORAGE_LOCK_RECOVER_CHANGE_FB, emFB));
    printf("RECOVER_DELETE size=%d\n", (int)sizeof(SNETPC_STORAGE_LOCK_RECOVER_DELETE));
    printf("RECOVER_DELETE_FB size=%d emFB=%d bStorageLockEnable=%d bStorageLockStatus=%d\n",
        (int)sizeof(SNETPC_STORAGE_LOCK_RECOVER_DELETE_FB), OF(SNETPC_STORAGE_LOCK_RECOVER_DELETE_FB, emFB),
        OF(SNETPC_STORAGE_LOCK_RECOVER_DELETE_FB, bStorageLockEnable),
        OF(SNETPC_STORAGE_LOCK_RECOVER_DELETE_FB, bStorageLockStatus));

    printf("--- struct layout: INVENTORY ---\n");
    printf("ENABLE size=%d szPin1=%d szPin2=%d\n",
        (int)sizeof(SNETPC_INVENTORY_LOCK_ENABLE), OF(SNETPC_INVENTORY_LOCK_ENABLE, szPin1),
        OF(SNETPC_INVENTORY_LOCK_ENABLE, szPin2));
    printf("ENABLE_FB size=%d emFB=%d bInventoryLockEnable=%d\n",
        (int)sizeof(SNETPC_INVENTORY_LOCK_ENABLE_FB), OF(SNETPC_INVENTORY_LOCK_ENABLE_FB, emFB),
        OF(SNETPC_INVENTORY_LOCK_ENABLE_FB, bInventoryLockEnable));
    printf("INPUT size=%d szPin=%d\n",
        (int)sizeof(SNETPC_INVENTORY_LOCK_INPUT), OF(SNETPC_INVENTORY_LOCK_INPUT, szPin));
    printf("INPUT_FB size=%d emFB=%d bInventoryLockStatus=%d\n",
        (int)sizeof(SNETPC_INVENTORY_LOCK_INPUT_FB), OF(SNETPC_INVENTORY_LOCK_INPUT_FB, emFB),
        OF(SNETPC_INVENTORY_LOCK_INPUT_FB, bInventoryLockStatus));
    printf("RECOVER size=%d\n", (int)sizeof(SNETPC_INVENTORY_LOCK_RECOVER));
    printf("RECOVER_FB size=%d emFB=%d szName=%d szPin=%d\n",
        (int)sizeof(SNETPC_INVENTORY_LOCK_RECOVER_FB), OF(SNETPC_INVENTORY_LOCK_RECOVER_FB, emFB),
        OF(SNETPC_INVENTORY_LOCK_RECOVER_FB, szName), OF(SNETPC_INVENTORY_LOCK_RECOVER_FB, szPin));
    printf("RECOVER_CHANGE size=%d szPin1=%d szPin2=%d\n",
        (int)sizeof(SNETPC_INVENTORY_LOCK_RECOVER_CHANGE), OF(SNETPC_INVENTORY_LOCK_RECOVER_CHANGE, szPin1),
        OF(SNETPC_INVENTORY_LOCK_RECOVER_CHANGE, szPin2));
    printf("RECOVER_CHANGE_FB size=%d emFB=%d\n",
        (int)sizeof(SNETPC_INVENTORY_LOCK_RECOVER_CHANGE_FB), OF(SNETPC_INVENTORY_LOCK_RECOVER_CHANGE_FB, emFB));
    printf("RECOVER_DELETE size=%d\n", (int)sizeof(SNETPC_INVENTORY_LOCK_RECOVER_DELETE));
    printf("RECOVER_DELETE_FB size=%d emFB=%d bInventoryLockEnable=%d bInventoryLockStatus=%d\n",
        (int)sizeof(SNETPC_INVENTORY_LOCK_RECOVER_DELETE_FB), OF(SNETPC_INVENTORY_LOCK_RECOVER_DELETE_FB, emFB),
        OF(SNETPC_INVENTORY_LOCK_RECOVER_DELETE_FB, bInventoryLockEnable),
        OF(SNETPC_INVENTORY_LOCK_RECOVER_DELETE_FB, bInventoryLockStatus));

    return 0;
}
