// partyprobe — MSVC sizeof/offsetof for the PARTY SEARCH (party-finder / "mini
// party link") wire structs, GLContrlPartyMsg.h. Needed because PARTYLINKDATA
// is DEFINED in GLCharData.h (no #pragma pack there) but EMBEDDED inside
// messages declared under GLContrlPartyMsg.h's #pragma pack(1) (lines 20-547)
// — whether the embedded copy ends up packed depends on MSVC's actual
// pack-state-at-point-of-parse behavior, not on hand reasoning, so it is
// measured directly rather than assumed. Same rule as every probe here:
// SOURCE/ is frozen; this only #includes it and prints ground truth.
//
// Build/run:  partyprobe.cmd

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
#include "GLContrlBaseMsg.h"
#include "GLCharData.h"
#include "GLContrlPartyMsg.h"

using namespace GLMSG;

#define L(f) ((int)offsetof(PARTYLINKDATA, f))
#define G(t, f) ((int)offsetof(t, f))

int main()
{
    printf("NET_MSG_GCTRL_GET_PARTYLIST=%d\n", (int)NET_MSG_GCTRL_GET_PARTYLIST);
    printf("NET_MSG_GCTRL_GET_PARTYLIST_FB=%d\n", (int)NET_MSG_GCTRL_GET_PARTYLIST_FB);
    printf("NET_MSG_GCTRL_PARTY_SEARCH_LURE=%d\n", (int)NET_MSG_GCTRL_PARTY_SEARCH_LURE);
    printf("NET_MSG_GCTRL_PARTY_SEARCH_LURE_FB=%d\n", (int)NET_MSG_GCTRL_PARTY_SEARCH_LURE_FB);
    printf("NET_MSG_GCTRL_PARTY_SEARCH_LURE_REQUESTOR_FB=%d\n", (int)NET_MSG_GCTRL_PARTY_SEARCH_LURE_REQUESTOR_FB);
    printf("NET_MSG_GCTRL_PARTY_SEARCH_LURE_TAR_ANS=%d\n", (int)NET_MSG_GCTRL_PARTY_SEARCH_LURE_TAR_ANS);
    printf("NET_MSG_GCTRL_REQ_PARTY_SEARCH_UPDATE=%d\n", (int)NET_MSG_GCTRL_REQ_PARTY_SEARCH_UPDATE);
    printf("PARTYLINKDATA size=%d\n", (int)sizeof(PARTYLINKDATA));
    printf("SPARTY_OPT size=%d\n", (int)sizeof(SPARTY_OPT));
    printf("EMCHARCLASS size=%d\n", (int)sizeof(EMCHARCLASS));
    printf("EMPARTY_LURE_FB size=%d\n", (int)sizeof(EMPARTY_LURE_FB));
    printf("EMREQ_PARTYLIST_FB size=%d\n", (int)sizeof(EMREQ_PARTYLIST_FB));
    printf("SNATIVEID size=%d\n", (int)sizeof(SNATIVEID));
    printf("CHAR_SZNAME=%d\n", (int)CHAR_SZNAME);

    printf("--- PARTYLINKDATA (standalone, GLCharData.h) ---\n");
    printf("dwIndex=%d\n",     L(dwIndex));
    printf("dwPartyID=%d\n",   L(dwPartyID));
    printf("m_dwGaeaID=%d\n",  L(m_dwGaeaID));
    printf("m_szName=%d\n",    L(m_szName));
    printf("m_emClass=%d\n",   L(m_emClass));
    printf("m_bPrivate=%d\n",  L(m_bPrivate));
    printf("m_sMapID=%d\n",    L(m_sMapID));
    printf("m_wLevel=%d\n",    L(m_wLevel));
    printf("m_wSchool=%d\n",   L(m_wSchool));
    printf("m_wMembers=%d\n",  L(m_wMembers));
    printf("sOption=%d\n",     L(sOption));

    printf("--- SNET_GET_PARTYLIST_FB (packed msg, embeds PARTYLINKDATA) ---\n");
    printf("size=%d\n", (int)sizeof(SNET_GET_PARTYLIST_FB));
    printf("emFB=%d\n", G(SNET_GET_PARTYLIST_FB, emFB));
    printf("sParty=%d\n", G(SNET_GET_PARTYLIST_FB, sParty));
    printf("sParty.m_dwGaeaID(embedded)=%d\n",
           (int)(offsetof(SNET_GET_PARTYLIST_FB, sParty) + offsetof(PARTYLINKDATA, m_dwGaeaID)));
    printf("sParty.m_szName(embedded)=%d\n",
           (int)(offsetof(SNET_GET_PARTYLIST_FB, sParty) + offsetof(PARTYLINKDATA, m_szName)));
    printf("sParty.m_wLevel(embedded)=%d\n",
           (int)(offsetof(SNET_GET_PARTYLIST_FB, sParty) + offsetof(PARTYLINKDATA, m_wLevel)));
    printf("sParty.m_wMembers(embedded)=%d\n",
           (int)(offsetof(SNET_GET_PARTYLIST_FB, sParty) + offsetof(PARTYLINKDATA, m_wMembers)));

    printf("--- SNET_PARTY_SEARCH_LURE (out) ---\n");
    printf("size=%d\n", (int)sizeof(SNET_PARTY_SEARCH_LURE));
    printf("dwGaeaID=%d\n", G(SNET_PARTY_SEARCH_LURE, dwGaeaID));
    printf("dwPartyID=%d\n", G(SNET_PARTY_SEARCH_LURE, dwPartyID));

    printf("--- SNET_PARTY_SEARCH_LURE_FB (in, target-side invite prompt) ---\n");
    printf("size=%d\n", (int)sizeof(SNET_PARTY_SEARCH_LURE_FB));
    printf("dwGaeaID=%d\n", G(SNET_PARTY_SEARCH_LURE_FB, dwGaeaID));
    printf("m_szName=%d\n", G(SNET_PARTY_SEARCH_LURE_FB, m_szName));
    printf("m_emClass=%d\n", G(SNET_PARTY_SEARCH_LURE_FB, m_emClass));
    printf("m_wLevel=%d\n", G(SNET_PARTY_SEARCH_LURE_FB, m_wLevel));
    printf("m_wSchool=%d\n", G(SNET_PARTY_SEARCH_LURE_FB, m_wSchool));

    printf("--- SNET_PARTY_SEARCH_LURE_REQUESTOR_FB (in, my request's result) ---\n");
    printf("size=%d\n", (int)sizeof(SNET_PARTY_SEARCH_LURE_REQUESTOR_FB));
    printf("dwMasterGaeaID=%d\n", G(SNET_PARTY_SEARCH_LURE_REQUESTOR_FB, dwMasterGaeaID));
    printf("dwMyGaeaID=%d\n", G(SNET_PARTY_SEARCH_LURE_REQUESTOR_FB, dwMyGaeaID));
    printf("m_szName=%d\n", G(SNET_PARTY_SEARCH_LURE_REQUESTOR_FB, m_szName));
    printf("emAnswer=%d\n", G(SNET_PARTY_SEARCH_LURE_REQUESTOR_FB, emAnswer));

    printf("--- SNET_PARTY_SEARCH_LURE_TAR_ANS (out, my answer to a prompt) ---\n");
    printf("size=%d\n", (int)sizeof(SNET_PARTY_SEARCH_LURE_TAR_ANS));
    printf("emAnswer=%d\n", G(SNET_PARTY_SEARCH_LURE_TAR_ANS, emAnswer));
    printf("dwPartyID=%d\n", G(SNET_PARTY_SEARCH_LURE_TAR_ANS, dwPartyID));
    printf("dwRequestorID=%d\n", G(SNET_PARTY_SEARCH_LURE_TAR_ANS, dwRequestorID));
    printf("sOption=%d\n", G(SNET_PARTY_SEARCH_LURE_TAR_ANS, sOption));
    return 0;
}
