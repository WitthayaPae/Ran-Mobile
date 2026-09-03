// Android entry point for the native RAN client.
// Phase 1: link target only — it pulls the game libraries in so the linker
// reports every platform symbol that still needs an implementation.
#include "windows.h"
#include "../../shim/platform/ran_plat.h"

#define LOGI(...) RanPlat_Log(RANLOG_INFO, "RAN", __VA_ARGS__)

extern "C" void RanShim_SetModulePath(const char *p);
extern "C" void RanShim_SetClientSize(int w, int h);

extern "C" void ran_native_boot(const char *dataRoot, int width, int height) {
    LOGI("[RAN] boot dataRoot=%s %dx%d", dataRoot ? dataRoot : "(null)", width, height);
    RanShim_SetModulePath(dataRoot);
    RanShim_SetClientSize(width, height);
}
