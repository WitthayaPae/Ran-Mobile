//  A tripwire on absurd allocations.
//
//  Entering the world aborted with std::bad_alloc on a request for
//  8389754676365913933 bytes — which is the ASCII of "Movement" read as a
//  size_t. A size like that is always a parse that went off the rails, and the
//  useful information is WHERE, so the request is logged with a backtrace
//  before the allocator is ever asked.
//
//  The guard only reports; it does not change behaviour, and anything under the
//  threshold goes straight to the normal allocator.

#include <stdlib.h>
#include <string.h>
#include <new>

#include <android/log.h>
#include <dlfcn.h>
#include <unwind.h>

namespace {

//  No sane single allocation in this client is this big; the largest real ones
//  are map/texture buffers in the tens of megabytes.
const size_t kAbsurd = 512u * 1024u * 1024u;

struct Frames {
    void  **slots;
    int     count;
    int     max;
};

_Unwind_Reason_Code collect(struct _Unwind_Context *ctx, void *arg) {
    Frames *f = (Frames *)arg;
    const uintptr_t pc = _Unwind_GetIP(ctx);
    if (pc && f->count < f->max) f->slots[f->count++] = (void *)pc;
    return f->count >= f->max ? _URC_END_OF_STACK : _URC_NO_REASON;
}

void reportAbsurd(size_t size) {
    __android_log_print(ANDROID_LOG_ERROR, "RanAlloc",
                        "absurd allocation: %zu bytes (0x%zx) — as text: \"%.8s\"",
                        size, size, (const char *)&size);

    void *pcs[24];
    Frames f = { pcs, 0, 24 };
    _Unwind_Backtrace(collect, &f);

    for (int i = 0; i < f.count; ++i) {
        Dl_info info;
        memset(&info, 0, sizeof(info));
        if (dladdr(pcs[i], &info) && info.dli_fname) {
            __android_log_print(ANDROID_LOG_ERROR, "RanAlloc", "  #%02d %p %s (%s+%td)",
                                i, pcs[i], info.dli_fname,
                                info.dli_sname ? info.dli_sname : "?",
                                info.dli_saddr ? (char *)pcs[i] - (char *)info.dli_saddr : 0);
        } else {
            __android_log_print(ANDROID_LOG_ERROR, "RanAlloc", "  #%02d %p", i, pcs[i]);
        }
    }
}

} // namespace

void *operator new(size_t size) {
    if (size >= kAbsurd) reportAbsurd(size);
    void *p = malloc(size ? size : 1);
    if (!p) throw std::bad_alloc();
    return p;
}

void *operator new[](size_t size) {
    if (size >= kAbsurd) reportAbsurd(size);
    void *p = malloc(size ? size : 1);
    if (!p) throw std::bad_alloc();
    return p;
}

void operator delete(void *p) throw()   { free(p); }
void operator delete[](void *p) throw() { free(p); }
void operator delete(void *p, size_t) throw()   { free(p); }
void operator delete[](void *p, size_t) throw() { free(p); }
