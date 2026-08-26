// petprobe — MSVC offsetof/sizeof for the pet wire structs (SDROPPET and the
// SNETPET_* messages that carry it).
//
// SDROPPET (GLPet.h) is defined at NATURAL alignment: GLContrlPetMsg.h
// includes GLPet.h at line 4, BEFORE its own #pragma pack(1) at line 113 —
// so SDROPPET's interior offsets keep natural padding, same trap already
// confirmed for SCHARSKILL this session (skillprobe.cpp). The message
// wrapper (SNETPET_DROP_PET etc, inside the pack(1) region) then embeds that
// already-fixed-size SDROPPET right after its 8-byte NET_MSG_GENERIC header.
// SOURCE/ is frozen — this only #includes it and prints ground truth.
// Build/run: petprobe.cmd

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
#include "GLItem.h"
#include "GLItemDef.h"
#include "GLContrlBaseMsg.h"
#include "GLPet.h"
#include "GLContrlPetMsg.h"

using namespace GLMSG;

#define OF(T, f) ((int)offsetof(T, f))

int main()
{
    printf("--- SDROPPET (GLPet.h, natural alignment at include point) ---\n");
    printf("SDROPPET size=%d\n", (int)sizeof(SDROPPET));
    printf("m_szName=%d m_emTYPE=%d m_dwGUID=%d m_sPetID=%d\n",
        OF(SDROPPET, m_szName), OF(SDROPPET, m_emTYPE), OF(SDROPPET, m_dwGUID), OF(SDROPPET, m_sPetID));
    printf("m_sActiveSkillID_A=%d m_sActiveSkillID_B=%d m_bDualSkill=%d\n",
        OF(SDROPPET, m_sActiveSkillID_A), OF(SDROPPET, m_sActiveSkillID_B), OF(SDROPPET, m_bDualSkill));
    printf("m_dwOwner=%d m_wStyle=%d m_wColor=%d m_fWalkSpeed=%d m_fRunSpeed=%d m_nFull=%d\n",
        OF(SDROPPET, m_dwOwner), OF(SDROPPET, m_wStyle), OF(SDROPPET, m_wColor),
        OF(SDROPPET, m_fWalkSpeed), OF(SDROPPET, m_fRunSpeed), OF(SDROPPET, m_nFull));
    printf("m_PutOnItems=%d (elem size=%d count=%d)\n",
        OF(SDROPPET, m_PutOnItems), (int)sizeof(SITEMCUSTOM), (int)PET_ACCETYPE_SIZE);
    printf("m_sMapID=%d m_dwCellID=%d m_vPos=%d m_vDir=%d m_vTarPos=%d\n",
        OF(SDROPPET, m_sMapID), OF(SDROPPET, m_dwCellID), OF(SDROPPET, m_vPos),
        OF(SDROPPET, m_vDir), OF(SDROPPET, m_vTarPos));
    printf("m_dwActionFlag=%d m_emPETACTYPE=%d m_petSkinPackData=%d m_wAniSub=%d m_dwPetID=%d\n",
        OF(SDROPPET, m_dwActionFlag), OF(SDROPPET, m_emPETACTYPE), OF(SDROPPET, m_petSkinPackData),
        OF(SDROPPET, m_wAniSub), OF(SDROPPET, m_dwPetID));
    printf("PETNAMESIZE=%d\n", (int)PETNAMESIZE);

    printf("--- SNETPET_DROP_PET (message wrapper, pack(1) region) ---\n");
    printf("SNETPET_DROP_PET size=%d Data=%d\n",
        (int)sizeof(SNETPET_DROP_PET), OF(SNETPET_DROP_PET, Data));
    printf("SNETPET_CREATE_ANYPET size=%d Data=%d\n",
        (int)sizeof(SNETPET_CREATE_ANYPET), OF(SNETPET_CREATE_ANYPET, Data));
    printf("SNETPET_REQ_DISAPPEAR size=%d dwGUID=%d\n",
        (int)sizeof(SNETPET_REQ_DISAPPEAR), OF(SNETPET_REQ_DISAPPEAR, dwGUID));

    return 0;
}
