// RanMobileApp — the mobile stand-in for CGameClient2Wnd.
//
// It is deliberately the SAME shape as the PC window class: it derives from the
// same CD3DApplication framework and implements the same virtuals, calling the
// same engine singletons in the same order. What is dropped is only what has no
// meaning here — the HackShield threads, the desktop cursor, the IE web control,
// the MFC window itself.
//
// Boot order mirrors CGameClient2App::InitInstance:
//   RANPARAM::LOAD -> DXPARAMSET::INIT -> RCC flags -> index Gui.rcc ->
//   CGameTextMan text -> CD3DApplication::Create -> frame loop
//
// Phase 2 runs this against the headless D3D9 shim: no pixels, but the real
// data load and the real network path.

#include "stdafx.h"
#include "../../shim/platform/ran_plat.h"

#include <sys/stat.h>
#include <time.h>

#include "../../shim/gl/gl_context.h"
#include "../../shim/gl/gl_render.h"

#include "d3dapp.h"
#include "RANPARAM.h"
#include "SUBPATH.h"
#include "dxparamset.h"
#include "GameTextControl.h"
#include "GLogic.h"
#include "Unzipper.h"
#include "DxResponseMan.h"
#include "DxViewPort.h"
#include "DxLightMan.h"
#include "DxGlobalStage.h"
#include "DxFontMan.h"
#include "d3dfont.h"
#include "DebugSet.h"
#include "DxFogMan.h"
#include "DxLightMan.h"
#include "ShaderConstant.h"
#include "NsOCTree.h"

#include <string>

#define LOGI(...) RanPlat_Log(RANLOG_INFO,  "RanApp", __VA_ARGS__)
#define LOGE(...) RanPlat_Log(RANLOG_ERROR, "RanApp", __VA_ARGS__)

extern "C" void RanShim_SetModulePath(const char *p);
extern "C" void RanShim_SetClientSize(int w, int h);
extern "C" void RanD3D_LogStats(void);
extern "C" void RanD3D_ReportBuffers(unsigned frames);
extern "C" void RanPath_ProbeVersionFile(void);

namespace {

char g_appPath[MAX_PATH] = {0};

// The engine dereferences the CWnd it is handed (DxResponseMan reads
// pWndApp->m_hWnd for DirectInput and the cursor). There is no window on
// mobile, but the object must exist — the shim CWnd is inert and its HWND is
// only ever passed to stubs.
CWnd g_dummyWnd;

class RanMobileApp : public CD3DApplication {
public:
    WORD m_width, m_height;
    bool m_created = false;

    RanMobileApp(int w, int h) : CD3DApplication(), m_width((WORD)w), m_height((WORD)h) {
        m_bUseDepthBuffer = TRUE;
    }

    // --- CGameClient2Wnd::OneTimeSceneInit, minus the web control ---
    HRESULT OneTimeSceneInit() override {
        LOGI("OneTimeSceneInit");
        HRESULT hr = DxResponseMan::GetInstance().OneTimeSceneInit(
            g_appPath, &g_dummyWnd, "D3DXFONT", RANPARAM::dwLangSet, RANPARAM::strGDIFont);
        if (FAILED(hr)) { LOGE("DxResponseMan::OneTimeSceneInit failed 0x%08X", (unsigned)hr); return E_FAIL; }
        return S_OK;
    }

    // --- CGameClient2Wnd::CreateObjects ---
    //
    // This override REPLACES the base class's version, so it owns the whole
    // device-object bring-up: fonts, then InitDeviceObjects, then
    // RestoreDeviceObjects. Forgetting the last two leaves DxGlobalStage
    // uninitialised and the first frame null-derefs — which is exactly what
    // happened when this only did the font half.
    //
    // Dropped from the PC version: the loading-screen thread (it renders, so it
    // belongs to phase 3) and the anti-cheat watchdog thread.
    HRESULT CreateObjects() override {
        LOGI("CreateObjects");
        DxFontMan::GetInstance().InitDeviceObjects(m_pd3dDevice);
        CD3DFontPar *pFont9 = DxFontMan::GetInstance().LoadDxFont(_DEFAULT_FONT, 9, _DEFAULT_FONT_FLAG);
        DxFontMan::GetInstance().LoadDxFont(_DEFAULT_FONT, 8, D3DFONT_SHADOW | D3DFONT_ASCII);
        CDebugSet::InitDeviceObjects(pFont9);

        HRESULT hr = InitDeviceObjects();
        if (FAILED(hr)) { LOGE("InitDeviceObjects failed 0x%08X", (unsigned)hr); return hr; }

        hr = RestoreDeviceObjects();
        if (FAILED(hr)) { LOGE("RestoreDeviceObjects failed 0x%08X", (unsigned)hr); return hr; }

        LOGI("CreateObjects done");
        return S_OK;
    }

    // --- CGameClient2Wnd::InitDeviceObjects, minus anti-cheat threads/cursor ---
    HRESULT InitDeviceObjects() override {
        LOGI("InitDeviceObjects");
        DxViewPort::GetInstance().InitDeviceObjects(m_pd3dDevice, NULL);
        DxResponseMan::GetInstance().InitDeviceObjects(m_pd3dDevice, TRUE);

        DXLIGHT sDirectional;
        sDirectional.SetDefault();
        sDirectional.m_Light.Diffuse = D3DXCOLOR(0, 0, 0, 1);
        sDirectional.m_Light.Ambient = D3DXCOLOR(0, 0, 0, 1);
        DxLightMan::SetDefDirect(sDirectional);

        DxGlobalStage::GetInstance().SetD3DApp(this);
        HRESULT hr = DxGlobalStage::GetInstance().OneTimeSceneInit(g_appPath, NULL, FALSE,
                                                                   m_width, m_height);
        if (FAILED(hr)) { LOGE("DxGlobalStage::OneTimeSceneInit failed 0x%08X", (unsigned)hr); return hr; }

        hr = DxGlobalStage::GetInstance().InitDeviceObjects(m_pd3dDevice);
        if (FAILED(hr)) { LOGE("DxGlobalStage::InitDeviceObjects failed 0x%08X", (unsigned)hr); return hr; }

        NSOCTREE::EnableDynamicLoad();
        LOGI("InitDeviceObjects done");
        return S_OK;
    }

    HRESULT RestoreDeviceObjects() override {
        DxResponseMan::GetInstance().RestoreDeviceObjects();
        DxGlobalStage::GetInstance().RestoreDeviceObjects();
        DXPARAMSET::INIT();
        return S_OK;
    }

    // --- CGameClient2Wnd::FrameMove, minus the anti-cheat watchdogs ---
    //
    // All THREE calls matter. Dropping DxViewPort::FrameMove leaves the camera's
    // clip volume uninitialised, and since every renderer culls against it, the
    // entire 3D world is discarded before it is drawn — a black scene with a
    // working UI on top, and no error anywhere.
    HRESULT FrameMove() override {
        DxGlobalStage::GetInstance().FrameMove(m_fTime, m_fElapsedTime);
        DxResponseMan::GetInstance().FrameMove(m_fTime, m_fElapsedTime, TRUE, TRUE);
        DxViewPort::GetInstance().FrameMove(m_fTime, m_fElapsedTime);
        return S_OK;
    }

    // --- CGameClient2Wnd::Render, minus the desktop cursor ---
    //
    // The light and camera constants are pushed exactly as the PC does: they feed
    // the vertex-shader paths, and the fixed-function paths depend on the
    // identity world matrix and the MODULATE stage state set alongside them.
    HRESULT Render() override {
        if (!m_pd3dDevice) return S_FALSE;

        DxFogMan::GetInstance().RenderFogSB(m_pd3dDevice);
        D3DCOLOR colorClear = DxFogMan::GetInstance().GetFogColor();

        HRESULT hr = m_pd3dDevice->Clear(0L, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
                                         colorClear, 1.0f, 0L);
        if (SUCCEEDED(hr = m_pd3dDevice->BeginScene())) {
            DxLightMan::GetInstance()->Render(m_pd3dDevice);

            DXLIGHT &Light = *DxLightMan::GetInstance()->GetDirectLight();
            m_pd3dDevice->SetVertexShaderConstantF(VSC_LIGHTDIRECT,  (float *)&Light.m_Light.Direction, 1);
            m_pd3dDevice->SetVertexShaderConstantF(VSC_LIGHTDIFFUSE, (float *)&Light.m_Light.Diffuse, 1);
            m_pd3dDevice->SetVertexShaderConstantF(VSC_LIGHTAMBIENT, (float *)&Light.m_Light.Ambient, 1);

            D3DXVECTOR3 &vFromPt = DxViewPort::GetInstance().GetFromPt();
            m_pd3dDevice->SetVertexShaderConstantF(VSC_CAMERAPOSITION, (float *)&vFromPt, 1);

            D3DLIGHTQ   pLight;
            D3DXVECTOR4 vPointPos;
            D3DXVECTOR3 vPointDiff;
            for (int i = 0; i < 7; ++i) {
                if (DxLightMan::GetInstance()->GetClosedLight(i + 1)) {
                    pLight = DxLightMan::GetInstance()->GetClosedLight(i + 1)->m_Light;
                    vPointDiff = D3DXVECTOR3(pLight.Diffuse.r, pLight.Diffuse.g, pLight.Diffuse.b);
                    vPointPos.x = pLight.Position.x;
                    vPointPos.y = pLight.Position.y;
                    vPointPos.z = pLight.Position.z;
                    vPointPos.w = pLight.Range;
                } else {
                    vPointPos = D3DXVECTOR4(0.f, 0.f, 0.f, 0.1f);
                    vPointDiff = D3DXVECTOR3(0.f, 0.f, 0.f);
                }
                m_pd3dDevice->SetVertexShaderConstantF(i * 2 + VSC_PLIGHTPOS01,  (float *)&vPointPos, 1);
                m_pd3dDevice->SetVertexShaderConstantF(i * 2 + VSC_PLIGHTDIFF01, (float *)&vPointDiff, 1);
            }

            D3DXMATRIX matView = DxViewPort::GetInstance().GetMatView();
            D3DXVECTOR4 vSkinDefault(1.f, 0.5f, 0.f, 765.01f);
            m_pd3dDevice->SetVertexShaderConstantF(VSC_SKIN_DEFAULT, (float *)&vSkinDefault, 1);

            D3DXVECTOR3 vLightVector = DxLightMan::GetInstance()->GetDirectLight()->m_Light.Direction;
            D3DXVec3TransformNormal(&vLightVector, &vLightVector, &matView);
            D3DXVec3Normalize(&vLightVector, &vLightVector);
            vLightVector = -vLightVector;
            m_pd3dDevice->SetVertexShaderConstantF(VSC_LIGHTDIRECT_VIEW, (float *)&vLightVector, 1);

            D3DXMATRIX matIdentity;
            D3DXMatrixIdentity(&matIdentity);
            m_pd3dDevice->SetTransform(D3DTS_WORLD, &matIdentity);

            m_pd3dDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);

            DxGlobalStage::GetInstance().Render();

            m_pd3dDevice->EndScene();
        }
        //  No Present here.
        //
        //  Render3DEnvironment calls Render() and then Present(), so presenting
        //  at the end of Render() swapped twice for every frame drawn. The
        //  second swap put up whichever buffer came next in the chain, which
        //  still held an older image - the loading screen, for a long time
        //  after the map had loaded. That is the flicker, and it is also a
        //  whole extra tile-buffer resolve a frame.
        return S_OK;
    }
};

RanMobileApp *g_app = NULL;

} // namespace

// ---------------------------------------------------------------- public API

// Mirrors CGameClient2App::InitInstance.
extern "C" void RanSplash_Step(void);

extern "C" int RanApp_Boot(const char *dataRoot, int width, int height) {
    LOGI("=== RAN mobile boot === root=%s %dx%d", dataRoot ? dataRoot : "(null)", width, height);

    strncpy(g_appPath, dataRoot ? dataRoot : "", MAX_PATH - 1);
    RanShim_SetModulePath(g_appPath);

    //  The client writes per-character options and its error log under the
    //  "Documents" folder, which on device is the data root; it uses fopen("wt")
    //  and never creates the directories itself.
    {
        static const char *kDirs[] = { "Logs", "Logs/PlayInfo", "Logs/ErrorLog", "Logs/Screenshot" };
        for (size_t i = 0; i < sizeof(kDirs) / sizeof(kDirs[0]); ++i) {
            std::string dir = std::string(g_appPath) + kDirs[i];
            mkdir(dir.c_str(), 0777);
        }
    }
    RanShim_SetClientSize(width, height);

    LOGI("step: RANPARAM::LOAD");
    RANPARAM::LOAD(g_appPath);
    LOGI("step: RANPARAM::LOAD done");
    RanSplash_Step();
    DXPARAMSET::INIT();
    LOGI("step: DXPARAMSET done");
    RanSplash_Step();
    LOGI("RANPARAM loaded — service=%d screen=%ux%u lang=%u",
         (int)RANPARAM::emSERVICE_TYPE, (unsigned)RANPARAM::dwScrWidth,
         (unsigned)RANPARAM::dwScrHeight, (unsigned)RANPARAM::dwLangSet);
    LOGI("login server: %s:%d", RANPARAM::LoginAddress, (int)RANPARAM::nLoginPort);

    // RCC is the shipping format for both glogic data and engine assets.
    GLOGIC::bGLOGIC_ZIPFILE = TRUE;
    //  The engine-level packs (Map/Animation/Effect/SkinObject.rcc) are optional:
    //  in zip mode DxLandMan extracts each map to a scratch dir before loading it,
    //  which on a phone means shipping a 548 MB Map.rcc AND writing a copy out per
    //  load. Loose files cost neither, so the mode follows what was actually pushed.
    {
        std::string mapRcc = std::string(g_appPath) + "Data/Map/Map.rcc";
        FILE *fp = fopen(mapRcc.c_str(), "rb");
        GLOGIC::bENGLIB_ZIPFILE = fp ? TRUE : FALSE;
        if (fp) fclose(fp);
        LOGI("engine data: %s", GLOGIC::bENGLIB_ZIPFILE ? "Map.rcc (zip mode)" : "loose files");
    }

    std::string guiRoot = std::string(g_appPath) + SUBPATH::GUI_FILE_ROOT;
    GLOGIC::strGUI_ZIPFILE = guiRoot + "Gui.rcc";
    if (!CUnzipper::LOADFILE_RCC(GLOGIC::strGUI_ZIPFILE))
        LOGE("Gui.rcc not indexed at %s — GUI will be empty", GLOGIC::strGUI_ZIPFILE.c_str());
    else
        LOGI("Gui.rcc indexed");
    RanSplash_Step();

    CGameTextMan::GetInstance().SetPath(guiRoot.c_str());
    CGameTextMan::GetInstance().LoadText(RANPARAM::strGameWord.GetString(),   CGameTextMan::EM_GAME_WORD,    RANPARAM::bXML_USE);
    CGameTextMan::GetInstance().LoadText(RANPARAM::strGameInText.GetString(), CGameTextMan::EM_GAME_IN_TEXT, RANPARAM::bXML_USE);
    CGameTextMan::GetInstance().LoadText(RANPARAM::strGameExText.GetString(), CGameTextMan::EM_GAME_EX_TEXT, RANPARAM::bXML_USE);
    LOGI("game text loaded");
    RanSplash_Step();

    g_app = new RanMobileApp(width, height);
    // The device surface is the only real mode. RANPARAM asks for a desktop
    // resolution that does not exist here, and the framework then searches its
    // mode list for it — so ask for what we actually have.
    g_app->SetScreen(width, height, EMSCREEN_F32, 60, TRUE);

    HRESULT hr = g_app->Create(NULL, NULL, NULL);
    if (FAILED(hr)) { LOGE("CD3DApplication::Create failed 0x%08X", (unsigned)hr); return 0; }

    g_app->m_created = true;
    RanPath_ProbeVersionFile();
    LOGI("=== boot complete ===");
    return 1;
}

// One iteration of the PC message loop's idle path.
extern "C" int RanApp_Frame(void) {
    if (!g_app || !g_app->m_created) return 0;
    if (FAILED(g_app->Render3DEnvironment())) return 0;
    return 1;
}

extern "C" void RanApp_Shutdown(void) {
    RanD3D_LogStats();
    if (g_app) { g_app->Cleanup3DEnvironment(); delete g_app; g_app = NULL; }
    LOGI("=== shutdown ===");
}

// ------------------------------------------------------------------ profiler
//  Section times reported by the game stage, accumulated between frame reports.
namespace {

struct ProfSection { const char *name; double seconds; unsigned calls; };
ProfSection g_sections[48];
unsigned    g_sectionCount = 0;

} // namespace

//  Last frame report, for the on-screen counter: reading a number off the
//  device beats asking for a logcat.
namespace { float g_lastFps = 0.f, g_lastEngine = 0.f, g_lastSubmit = 0.f, g_lastSwap = 0.f; }

extern "C" int RanProf_TexturesCompressed(void) { return RanGLR_TexturesCompressed(); }

extern "C" void RanProf_Get(float *fps, float *engineMs, float *submitMs, float *swapMs) {
    if (fps) *fps = g_lastFps;
    if (engineMs) *engineMs = g_lastEngine;
    if (submitMs) *submitMs = g_lastSubmit;
    if (swapMs) *swapMs = g_lastSwap;
}

namespace { double g_collSeconds = 0.0; unsigned long g_collCalls = 0; }

//  A clock the client's own code can time a phase with, and a per-frame count
//  to divide that phase by.
//
//  A total is not useful on its own here: the question is what one more player
//  or one more mob costs, because the scene this has to survive is a hundred of
//  each, not the dozen that happens to be standing around.
extern "C" double RanProf_Now(void) { return DXUtil_Timer(TIMER_GETABSOLUTETIME); }

namespace {
struct ProfCount { const char *name; unsigned long total; unsigned long frames; };
ProfCount g_counts[12];
unsigned  g_countCount = 0;
}

extern "C" void RanProf_Count(const char *szName, int nCount) {
    for (unsigned i = 0; i < g_countCount; ++i) {
        if (g_counts[i].name == szName) {
            g_counts[i].total += (unsigned long)(nCount < 0 ? 0 : nCount);
            ++g_counts[i].frames;
            return;
        }
    }
    if (g_countCount >= 12) return;
    g_counts[g_countCount].name = szName;
    g_counts[g_countCount].total = (unsigned long)(nCount < 0 ? 0 : nCount);
    g_counts[g_countCount].frames = 1;
    ++g_countCount;
}

extern "C" void RanProf_Collision(double fSeconds) { g_collSeconds += fSeconds; ++g_collCalls; }

extern "C" void RanProf_Section(const char *szName, double fSeconds) {
    for (unsigned i = 0; i < g_sectionCount; ++i) {
        if (g_sections[i].name == szName) {
            g_sections[i].seconds += fSeconds;
            ++g_sections[i].calls;
            return;
        }
    }
    if (g_sectionCount >= 48) return;
    g_sections[g_sectionCount].name = szName;
    g_sections[g_sectionCount].seconds = fSeconds;
    g_sections[g_sectionCount].calls = 1;
    ++g_sectionCount;
}

//  The frame split, reported once a second. The three numbers say which side
//  of the port is slow: the client's own update, the draw submission, or the
//  swap - and on a tiled GPU the swap is where the frame is actually drawn, so
//  a big Present with a small Render means the GPU is the limit, not the shim.
extern "C" void RanD3D_LiveMemLine(char *out, int cap);
extern "C" void RanD3D_HeldMemLine(char *out, int cap);

extern "C" void RanProf_Frame(double fUpdate, double fRender, double fPresent) {
    static double s_update = 0.0, s_render = 0.0, s_present = 0.0, s_last = 0.0;
    static unsigned s_frames = 0;

    s_update += fUpdate; s_render += fRender; s_present += fPresent;
    ++s_frames;

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    const double now = (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
    if (s_last == 0.0) { s_last = now; return; }
    if (now - s_last < 1.0) return;

    const double total = s_update + s_render + s_present;
    //  The swap and the draw submission both happen inside Render, so name
    //  them separately: what is left is the engine's own CPU work.
    const double swap = RanGL_TakeSwapSeconds();
    const double submit = RanGLR_TakeDrawSeconds();
    RanGLR_RefreshDiagnostics();
    const unsigned long drawCount = RanGLR_TakeDrawCount();
    unsigned long bufCount = 0, bufBytes = 0; double bufSeconds = 0.0;
    RanGLR_TakeBufferStats(&bufCount, &bufBytes, &bufSeconds);
    LOGI("FRAME %.1f fps | %.1f ms = update %.1f + render %.1f + present %.1f "
         "[of render: swap %.1f, submit %.1f, engine cpu %.1f]",
         s_frames / (now - s_last), total * 1000.0 / s_frames,
         s_update * 1000.0 / s_frames, s_render * 1000.0 / s_frames,
         s_present * 1000.0 / s_frames,
         swap * 1000.0 / s_frames, submit * 1000.0 / s_frames,
         (s_render - swap - submit) * 1000.0 / s_frames);
    {
        //  Live texture and buffer memory by owner - the same line iOS prints
        //  beside its footprint, so a crowd's cost can be split on either.
        char own[200];
        RanD3D_LiveMemLine(own, sizeof(own));
        LOGI("MEM owners: %s", own);
        char held[600];
        RanD3D_HeldMemLine(held, sizeof(held));
        LOGI("MEM RAM copies: %s", held);
    }
    LOGI("FRAME collision: %lu rays/frame, %.1f ms/frame",
         g_collCalls / s_frames, g_collSeconds * 1000.0 / s_frames);
    g_collSeconds = 0.0; g_collCalls = 0;
    {
        unsigned long cu=0, ct=0, ca=0, cs=0, cd=0, cb=0;
        RanGLR_TakeCallCounts(&cu, &ct, &ca, &cs, &cd, &cb);
        const unsigned long total = cu + ct + ca + cs + cd + cb;
        LOGI("FRAME gl calls: %lu/frame = uniform %lu, texture %lu, attrib %lu, draw %lu, buffer %lu",
             total / s_frames, cu / s_frames, ct / s_frames, ca / s_frames, cd / s_frames, cb / s_frames);
        unsigned long u[10];
        RanGLR_TakeUniformCounts(u);
        LOGI("FRAME uniforms/frame: palette %lu calls %lu KB, matrix %lu calls %lu KB, "
             "small %lu calls %lu KB, lights %lu calls %lu KB, uncached %lu calls %lu KB",
             u[0] / s_frames, u[1] / 1024 / s_frames, u[2] / s_frames, u[3] / 1024 / s_frames,
             u[4] / s_frames, u[5] / 1024 / s_frames, u[6] / s_frames, u[7] / 1024 / s_frames,
             u[8] / s_frames, u[9] / 1024 / s_frames);
        unsigned long pd[6], pu[6];
        RanGLR_TakePaletteUse(pd, pu);
        LOGI("FRAME palette/frame (draws/uploads): none %lu/%lu, blend1 %lu/%lu, blend2 %lu/%lu, "
             "blend3 %lu/%lu, more %lu/%lu, indexed %lu/%lu",
             pd[0] / s_frames, pu[0] / s_frames, pd[1] / s_frames, pu[1] / s_frames,
             pd[2] / s_frames, pu[2] / s_frames, pd[3] / s_frames, pu[3] / s_frames,
             pd[4] / s_frames, pu[4] / s_frames, pd[5] / s_frames, pu[5] / s_frames);
        unsigned long ph[5], psum = 0;
        RanGLR_TakePaletteSlots(ph, &psum);
        const unsigned long pn = ph[0] + ph[1] + ph[2] + ph[3] + ph[4];
        LOGI("FRAME palette slots/frame (indexed draws): 1-4 %lu, 5-8 %lu, 9-12 %lu, 13-16 %lu, "
             "more %lu, mean %.1f",
             ph[0] / s_frames, ph[1] / s_frames, ph[2] / s_frames, ph[3] / s_frames,
             ph[4] / s_frames, pn ? (double)psum / (double)pn : 0.0);
        unsigned long ups = 0, uvc = 0;
        RanGLR_TakeProgramSwitches(&ups, &uvc);
        LOGI("FRAME programs/frame: %lu glUseProgram, %lu variant changes", ups / s_frames, uvc / s_frames);
        RanGLR_ReportVariantFlips(s_frames);
        unsigned long afr = 0, avb = 0;
        RanGLR_TakeAttribCounts(&afr, &avb);
        LOGI("FRAME attributes/frame: %lu format re-specs (~21 GL calls each), %lu vertex buffer binds",
             afr / s_frames, avb / s_frames);
        unsigned long upc = 0, upb = 0;
        RanGLR_TakeUpStream(&upc, &upb);
        LOGI("FRAME draw-path stream/frame: %lu writes, %lu KB", upc / s_frames, upb / 1024 / s_frames);
        RanGLR_ReportStreamSections(s_frames);
        unsigned long lc[3];
        RanGLR_TakeLightCauses(lc);
        LOGI("FRAME light block uploads/frame by cause: program cache %lu, count %lu, values %lu",
             lc[0] / s_frames, lc[1] / s_frames, lc[2] / s_frames);
    }
    LOGI("FRAME draws: %lu per frame, %.0f us each",
         drawCount / s_frames,
         drawCount ? submit * 1e6 / drawCount : 0.0);
    LOGI("FRAME buffers: %lu uploads/frame, %lu KB/frame, %.1f ms/frame",
         bufCount / s_frames, bufBytes / 1024 / s_frames, bufSeconds * 1000.0 / s_frames);
    RanD3D_ReportBuffers(s_frames);
    RanGLR_ReportBufferKinds(s_frames);
    RanGLR_ReportGpuSections(s_frames);

    //  Sections, worst first: the frame's time in the client's own render.
    {
        char line[768] = "FRAME sections:";
        for (unsigned pass = 0; pass < g_sectionCount && pass < 10; ++pass) {
            int worst = -1;
            for (unsigned i = 0; i < g_sectionCount; ++i)
                if (g_sections[i].seconds > 0.0 &&
                    (worst < 0 || g_sections[i].seconds > g_sections[worst].seconds))
                    worst = (int)i;
            if (worst < 0) break;
            char one[80];
            snprintf(one, sizeof(one), " %s %.1fms", g_sections[worst].name,
                     g_sections[worst].seconds * 1000.0 / s_frames);
            strncat(line, one, sizeof(line) - strlen(line) - 1);
            g_sections[worst].seconds = -1.0;               // taken
        }
        if (g_sectionCount) LOGI("%s", line);
        for (unsigned i = 0; i < g_sectionCount; ++i) { g_sections[i].seconds = 0.0; g_sections[i].calls = 0; }
    }

    //  How many entities went through those phases, and what one costs. This is
    //  the number that says whether a hundred players will fit in a frame.
    if (g_countCount) {
        char line[512] = "FRAME counts:";
        for (unsigned i = 0; i < g_countCount; ++i) {
            const double per = g_counts[i].frames ? (double)g_counts[i].total / g_counts[i].frames : 0.0;
            char one[80];
            snprintf(one, sizeof(one), " %s %.1f/frame", g_counts[i].name, per);
            strncat(line, one, sizeof(line) - strlen(line) - 1);
            g_counts[i].total = 0; g_counts[i].frames = 0;
        }
        LOGI("%s", line);
    }

    g_lastFps    = (float)(s_frames / (now - s_last));
    g_lastEngine = (float)((s_render - swap - submit) * 1000.0 / s_frames);
    g_lastSubmit = (float)(submit * 1000.0 / s_frames);
    g_lastSwap   = (float)(swap * 1000.0 / s_frames);

    s_update = s_render = s_present = 0.0;
    s_frames = 0;
    s_last = now;
}
