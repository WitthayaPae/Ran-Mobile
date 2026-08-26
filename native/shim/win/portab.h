#pragma once
// Tik's portab.h is the old LZO demo portability layer; on mobile we only need
// the malloc helpers it provided.
#include <stdlib.h>
#include "lzo/lzoconf.h"
#ifndef lzo_malloc
#define lzo_malloc(a)     malloc(a)
#define lzo_free(a)       free(a)
#endif
