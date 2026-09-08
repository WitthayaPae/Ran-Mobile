// Windows path -> Android path resolution.
//
// The client was written for a case-insensitive filesystem with backslash
// separators. Its SUBPATH tables say things like "\Data\GUI\" while the shipped
// tree on disk is "data/gui". On Windows both the separator and the case are
// irrelevant; on Android neither is, so EVERY file open would fail without this.
//
// Resolution is:
//   1. backslashes -> forward slashes, collapse doubled separators
//   2. if the path exists as written, use it (the fast, common case)
//   3. otherwise walk it component by component, matching each against the real
//      directory entries case-insensitively
//
// Results are cached, because the engine opens tens of thousands of files and a
// readdir per component per open would dominate load time.

#include "windows.h"
#include "../platform/ran_plat.h"

#include <dirent.h>
#include <errno.h>
#if defined(__APPLE__)
#include <fcntl.h>               //  the /proc-free descriptor count
#include <sys/resource.h>
#endif
#include <stdio.h>
#include <sys/stat.h>
#include <strings.h>
#include <map>
#include <set>
#include <string>
#include <mutex>

#define LOGW(...) RanPlat_Log(RANLOG_WARN, "RanPath", __VA_ARGS__)

namespace {

std::mutex g_lock;
std::map<std::string, std::string> g_cache;      // requested -> resolved
unsigned long g_hits = 0, g_walks = 0, g_misses = 0;

bool exists(const std::string &p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0;
}

// Find `name` inside `dir` ignoring case. Returns the real spelling.
bool matchEntry(const std::string &dir, const std::string &name, std::string &out) {
    DIR *d = opendir(dir.empty() ? "/" : dir.c_str());
    if (!d) return false;
    bool found = false;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (strcasecmp(e->d_name, name.c_str()) == 0) { out = e->d_name; found = true; break; }
    }
    closedir(d);
    return found;
}

std::string normalise(const char *in) {
    std::string s(in ? in : "");
    for (auto &c : s) if (c == '\\') c = '/';
    // collapse "//" but keep a leading one (harmless on Android, and this never
    // sees UNC paths)
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '/' && !out.empty() && out.back() == '/') continue;
        out += s[i];
    }
    return out;
}

} // namespace

//  Creating a directory needs its own path handling: RanPath_Resolve maps a
//  path onto files that already exist, and the last component of a new
//  directory by definition does not. Without this the client's Windows-style
//  "\Data\Map\RanMapZipTemp\\" arrived at mkdir() verbatim and Android
//  happily created a single file whose NAME contained backslashes.
extern "C" int RanPath_MakeDir(const char *in) {
    if (!in || !*in) return -1;
    std::string want = normalise(in);

    std::string cur = (want[0] == '/') ? "/" : "";
    size_t i = (want[0] == '/') ? 1 : 0;
    int result = 0;

    while (i <= want.size()) {
        const size_t slash = want.find('/', i);
        const std::string comp = want.substr(i, (slash == std::string::npos ? want.size() : slash) - i);
        if (!comp.empty()) {
            std::string probe = cur;
            if (!probe.empty() && probe.back() != '/') probe += '/';
            probe += comp;

            if (exists(probe)) {
                cur = probe;
            } else {
                //  Case may differ from what the client asked for, exactly as
                //  when opening a file.
                std::string real;
                if (matchEntry(cur.empty() ? "." : cur, comp, real)) {
                    std::string alt = cur;
                    if (!alt.empty() && alt.back() != '/') alt += '/';
                    alt += real;
                    cur = alt;
                } else {
                    result = mkdir(probe.c_str(), 0777);
                    if (result != 0 && errno == EEXIST) result = 0;
                    cur = probe;
                }
            }
        }
        if (slash == std::string::npos) break;
        i = slash + 1;
    }
    return result;
}

extern "C" const char *RanPath_Resolve(const char *in) {
    // Returned pointer is owned by the cache and stays valid for the process.
    static thread_local std::string fallback;
    if (!in || !*in) return in;

    std::string req = normalise(in);

    std::lock_guard<std::mutex> guard(g_lock);
    auto it = g_cache.find(req);
    if (it != g_cache.end()) { ++g_hits; return it->second.c_str(); }

    if (exists(req)) {
        ++g_hits;
        return g_cache.emplace(req, req).first->second.c_str();
    }

    // Walk component by component, fixing case as we go.
    ++g_walks;
    std::string cur = (req[0] == '/') ? "/" : "";
    size_t i = (req[0] == '/') ? 1 : 0;
    bool ok = true;
    while (i <= req.size()) {
        size_t slash = req.find('/', i);
        std::string comp = req.substr(i, (slash == std::string::npos ? req.size() : slash) - i);
        if (!comp.empty()) {
            std::string probe = cur;
            if (!probe.empty() && probe.back() != '/') probe += '/';
            probe += comp;
            if (exists(probe)) {
                cur = probe;
            } else {
                std::string dir = cur.empty() ? "." : cur;
                std::string real;
                if (matchEntry(dir, comp, real)) {
                    if (!cur.empty() && cur.back() != '/') cur += '/';
                    cur += real;
                } else {
                    // Component genuinely absent. Keep the rest verbatim so the
                    // caller's own "file not found" handling reports the path it
                    // asked for, not a half-resolved one.
                    ok = false;
                    if (!cur.empty() && cur.back() != '/') cur += '/';
                    cur += req.substr(i);
                    break;
                }
            }
        }
        if (slash == std::string::npos) break;
        i = slash + 1;
        if (i <= req.size() && !cur.empty() && cur.back() != '/') cur += '/';
    }

    if (!ok) ++g_misses;
    // Loud on a cadence: if the boot ever appears to hang, the last line printed
    // here says exactly which path the engine was chasing.
    if (((g_walks + g_misses) % 200) == 0)
        RanPlat_Log(RANLOG_INFO, "RanPath", "resolve #%lu %s -> %s%s",
                            g_walks, req.c_str(), cur.c_str(), ok ? "" : "  (MISSING)");
    return g_cache.emplace(req, cur).first->second.c_str();
}

//  Failed opens that were not worth logging again. See ran_fopen.
unsigned long g_failedRepeats = 0;

//  ------------------------------------------------- who asked for that path
//
//  A path shaped like the app's data directory and nothing more - 33 bytes of
//  "/storage/emulated/0/Android/data/" followed by a name and no further
//  separator - reaches here about 16 times a session. It is already corrupt
//  when ran_fopen receives it (requested and resolved are the same bytes), the
//  resolver's cache cannot dangle, and it is always preceded by "piece material
//  got no texture: (no name)".
//
//  No amount of reading has said WHICH caller, so ask the stack. Nothing else
//  ever opens a bare directory path, so the test cannot fire on legitimate
//  work, and it is capped either way.
#ifdef __ANDROID__
#include <unwind.h>
#include <dlfcn.h>
#include <stdint.h>

namespace {

struct BtState { void **cur; void **end; };

_Unwind_Reason_Code BtFrame ( struct _Unwind_Context *ctx, void *arg ) {
    BtState *s = (BtState *) arg;
    const uintptr_t pc = _Unwind_GetIP ( ctx );
    if (pc) {
        if (s->cur == s->end) return _URC_END_OF_STACK;
        *s->cur++ = (void *) pc;
    }
    return _URC_NO_REASON;
}

//  The offset from the library's load address is the useful number: it is what
//  llvm-symbolizer takes against the unstripped out/<abi>/libran.so.
void LogBacktrace ( const char *path ) {
    void *frames[40];
    BtState st = { frames, frames + 40 };
    _Unwind_Backtrace ( BtFrame, &st );
    const size_t n = (size_t)( st.cur - frames );
    RanPlat_Log ( RANLOG_ERROR, "RanOpen", "  who asked for '%s' - %zu frames:", path, n );
    for (size_t i = 0; i < n; ++i) {
        Dl_info info;
        const char *sym = "?", *lib = "?";
        unsigned long off = 0;
        if (dladdr ( frames[i], &info ) && info.dli_fbase) {
            lib = info.dli_fname ? info.dli_fname : "?";
            if (info.dli_sname) sym = info.dli_sname;
            off = (unsigned long)( (const char *) frames[i] - (const char *) info.dli_fbase );
        }
        RanPlat_Log ( RANLOG_ERROR, "RanOpen", "    #%02zu  %s+0x%lx  %s",
                      i, lib, off, sym );
    }
}

//  "…/Android/data/<something>" with no separator after it. A file open never
//  looks like that; only a mangled copy of the data root does.
bool LooksLikeBareDataDir ( const char *path ) {
    const char *m = strstr ( path, "/Android/data/" );
    if (!m) return false;
    const char *tail = m + 14;              //  strlen("/Android/data/")
    return *tail != 0 && strchr ( tail, '/' ) == NULL;
}

}   // namespace
#endif  //  __ANDROID__

extern "C" void RanPath_LogStats(void) {
    std::lock_guard<std::mutex> guard(g_lock);
    RanPlat_Log(RANLOG_INFO, "RanPath",
        "path cache — %zu entries, %lu direct hits, %lu case-walks, %lu not found, "
        "%lu repeat failed opens",
        g_cache.size(), g_hits, g_walks, g_misses, g_failedRepeats);
}

// The engine calls fopen directly in many places; windows.h redirects it here.
//
// #undef rather than a header guard: this TU is compiled with a force-included
// StdAfx.h, so windows.h has already been processed before line 1 of this file
// and any "define RAN_PATH_IMPL first" trick comes too late — which is exactly
// how this recursed into itself the first time.
#undef fopen
extern "C" FILE *ran_fopen(const char *path, const char *mode) {
    const char *real = RanPath_Resolve(path);
    FILE *f = fopen(real, mode);
    //  Successes are capped (a boot opens thousands of files); FAILURES are
    //  always reported - a missing file is the single most common cause of an
    //  empty screen, and capping those hid several already.
    //  Always report the version file: it decides whether the client believes
    //  its own install is intact, and a silent success tells us nothing.
    if (strstr(path, "cVer") || strstr(path, "cver")) {
        long sz = -1;
        if (f) { fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET); }
        RanPlat_Log(RANLOG_INFO, "RanOpen", "version file: %s -> %s size=%ld",
                            path, f ? real : "FAILED", sz);
    }
    static unsigned n = 0;
    if (!f) {
        //  Once per distinct path, not once per open.
        //
        //  The client asks for GLogicServer.rcc - server data that never ships
        //  to players - 5,558 times during boot, and every one of those used to
        //  cost four log lines, two hex dumps and a SECOND fopen to retry. That
        //  was 0.83 seconds of the boot and 22,300 log lines, which is more than
        //  logd keeps: the real boot log was being pushed out by the noise.
        //
        //  The dump itself is still worth having the first time a path fails -
        //  it is what identifies an invisible character on the end - so it is
        //  kept for the first sighting of each path, and capped.
        static std::set<std::string> s_said;
        const bool first = ( s_said.size() < 64 ) && s_said.insert(path).second;
        //  Counted, not conflated with g_misses: that one counts paths the
        //  resolver could not resolve, which is a different question from a
        //  path that resolved and then would not open.
#ifdef __ANDROID__
        //  Before the repeat cap, and with a cap of its own: the shape is what
        //  is being chased, and the first sighting may already have been eaten
        //  by s_said above.
        {
            static int s_traces = 0;
            if (s_traces < 4 && LooksLikeBareDataDir ( path )) {
                ++s_traces;
                LogBacktrace ( path );
            }
        }
#endif
        if (!first) { ++g_failedRepeats; return NULL; }

        RanPlat_Log(RANLOG_ERROR, "RanOpen", "%s %s -> FAILED (resolved: %s, errno %d)",
                            mode, path, real ? real : "?", errno);
        //  A path that looks right but will not open usually has an invisible
        //  character on the end, so the bytes are dumped rather than the text.
        if (real) {
            char hex[128] = {0};
            size_t n = strlen(real);
            size_t from = n > 6 ? n - 6 : 0;
            for (size_t i = from, k = 0; i < n && k + 3 < sizeof(hex); ++i, k += 3)
                snprintf(hex + k, sizeof(hex) - k, "%02X ", (unsigned char)real[i]);
            RanPlat_Log(RANLOG_ERROR, "RanOpen", "    len=%zu tail=%s", n, hex);
            //  Same bytes through a literal, to tell a bad string from a bad
            //  environment.
            char full[256] = {0};
            for (size_t i = 0, k = 0; i < n && k + 3 < sizeof(full); ++i, k += 3)
                snprintf(full + k, sizeof(full) - k, "%02X ", (unsigned char)real[i]);
            RanPlat_Log(RANLOG_ERROR, "RanOpen", "    hex=%s", full);
            FILE *again = fopen(real, mode);
            RanPlat_Log(RANLOG_ERROR, "RanOpen", "    retry same string: %s (errno %d)",
                                again ? "OK" : "FAILED", again ? 0 : errno);
            if (again) fclose(again);
        }
    } else if (n < 300) {
        ++n;
        RanPlat_Log(RANLOG_INFO, "RanOpen", "%s %s -> %s", mode, path, real);
    }
    return f;
}

// Called from CIniLoader's parse loop (RAN_MOBILE only) so a stall inside ini
// parsing is visible instead of silent.
extern "C" void RanLog_IniProgress(const char *file, unsigned lines) {
    RanPlat_Log(RANLOG_INFO, "RanIni", "%s: %u lines", file ? file : "?", lines);
}

// Reports the result of a recursive file-tree scan (TextureManager and friends).
// An empty tree is a silent failure on device: every later lookup misses and the
// screen just stays blank, so the count is logged where the scan happens.
extern "C" void RanLog_FileTree(const char *path, int count) {
    RanPlat_Log(count ? RANLOG_INFO : RANLOG_ERROR, "RanTree",
                        "%s: %d files", path ? path : "?", count);
}

// Engine diagnostics (CDebugSet::ToLogFile and friends). On Windows these go to
// a file under the user profile; here that path does not exist, so logcat is the
// only place they can be seen.
extern "C" void RanLog_Engine(const char *msg) {
    RanPlat_Log(RANLOG_WARN, "RanEngine", "%s", msg ? msg : "");
}

// What a map load produced: frame count and leaf-node count. An empty screen
// with a "successful" load is otherwise indistinguishable from a failed one.
extern "C" void RanLog_Land(const char *file, int frames, int leafNodes) {
    RanPlat_Log(frames ? RANLOG_INFO : RANLOG_ERROR, "RanLand",
                        "%s: %d frames, %d leaf nodes", file ? file : "?", frames, leafNodes);
}

// What the .wld0 sidecar produced. The .wld holds only the octree skeleton; if
// these counts are zero the map is loaded but has no geometry to draw.
extern "C" void RanLog_StaticMesh(int solid, int alpha, int softAlpha) {
    RanPlat_Log((solid || alpha || softAlpha) ? RANLOG_INFO : RANLOG_ERROR,
                        "RanMesh", "static mesh: %d solid, %d alpha, %d soft-alpha",
                        solid, alpha, softAlpha);
}

// Stream position at each section boundary of a .wld load. A desync shows up as
// the exact section whose byte count went wrong.
extern "C" void RanLog_Section(const char *name, long pos) {
    RanPlat_Log(RANLOG_INFO, "RanWld", "%-18s @ %ld", name ? name : "?", pos);
}

// A serialized element count. An absurd value is the signature of a stream that
// desynced earlier, and names the section that did it.
extern "C" void RanLog_Count(const char *what, unsigned count, long pos) {
    RanPlat_Log(RANLOG_INFO, "RanWld", "%s: %u (@ %ld)", what ? what : "?", count, pos);
}

// One line per static-mesh tree node visited, and whether the frustum kept it.
// Only the first few: the question is "any at all", not the count.
extern "C" void RanLog_Cull(int culled, float maxx, float maxy, float maxz,
                            float minx, float miny, float minz) {
    static int n = 0;
    if (n >= 12) return;
    ++n;
    if (culled) RanPlat_Log(RANLOG_INFO, "RanCull", "  -> culled");
    else RanPlat_Log(RANLOG_INFO, "RanCull",
                             "node max(%.0f,%.0f,%.0f) min(%.0f,%.0f,%.0f)",
                             maxx, maxy, maxz, minx, miny, minz);
}

// The frustum the engine culls against. All-zero planes cull the entire world,
// which looks exactly like a scene that failed to load.
extern "C" void RanLog_CV(float ex, float ey, float ez, float fov, float w, float h,
                          float na, float nb, float nc, float nd,
                          float la, float lb, float lc, float ld) {
    static int n = 0;
    if (n >= 3) return;
    ++n;
    RanPlat_Log(RANLOG_INFO, "RanCV",
                        "eye(%.1f,%.1f,%.1f) fov=%.3f %.0fx%.0f near(%.3f,%.3f,%.3f,%.1f) left(%.3f,%.3f,%.3f,%.1f)",
                        ex, ey, ez, fov, w, h, na, nb, nc, nd, la, lb, lc, ld);
}

// One-shot probe: can this process open the version file at all, and how many
// descriptors are already open? A file that `ls` shows but fopen cannot see is
// usually a descriptor-limit or namespace problem, not a path problem.
extern "C" void RanPath_ProbeVersionFile(void) {
    const char *paths[] = { "/sdcard/ran/cVer.bin", "/sdcard/ran/param.ini" };
    for (int i = 0; i < 2; ++i) {
        FILE *f = fopen(paths[i], "rb");
        RanPlat_Log(f ? RANLOG_INFO : RANLOG_ERROR, "RanProbe",
                            "%s -> %s (errno %d)", paths[i], f ? "OK" : "FAILED", f ? 0 : errno);
        if (f) fclose(f);
    }
    //  /proc is Linux. On iOS the same count comes from asking about every
    //  descriptor up to the limit - cheap enough for a one-shot probe.
    int open = 0;
#if defined(__APPLE__)
    struct rlimit rl;
    const int top = (getrlimit(RLIMIT_NOFILE, &rl) == 0 && rl.rlim_cur < 4096)
                  ? (int)rl.rlim_cur : 4096;
    for (int fd = 0; fd < top; ++fd) if (fcntl(fd, F_GETFD) != -1) ++open;
#else
    DIR *d = opendir("/proc/self/fd");
    if (d) { while (readdir(d)) ++open; closedir(d); }
#endif
    RanPlat_Log(RANLOG_INFO, "RanProbe", "open descriptors: %d", open);
}

// The login feedback, as the client sees it: the server's verdict, whether the
// local version file could be read, and the two version pairs it compares.
extern "C" void RanLog_Login(int result, int verFileOk, int clientPatch, int clientGame,
                             int serverPatch, int serverGame) {
    RanPlat_Log(RANLOG_INFO, "RanLogin",
                        "result=%d verFile=%s client=(%d,%d) server=(%d,%d)",
                        result, verFileOk ? "ok" : "UNREADABLE",
                        clientPatch, clientGame, serverPatch, serverGame);
}
