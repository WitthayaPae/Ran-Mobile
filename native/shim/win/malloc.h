#pragma once
//  Darwin has no <malloc.h> - the platform header is <malloc/malloc.h>. An
//  include_next would look for a file that is not there and stop the build on
//  the first translation unit that pulls this in, which is most of them.
#if defined(__APPLE__)
#include <malloc/malloc.h>
#else
#include_next <malloc.h>
#endif
#include <alloca.h>
