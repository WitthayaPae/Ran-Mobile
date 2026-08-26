#pragma once
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "windows.h"
#define _O_RDONLY O_RDONLY
#define _O_BINARY 0
#define _S_IREAD  S_IRUSR
#define _S_IWRITE S_IWUSR
struct _finddata_t { unsigned attrib; long time_create, time_access, time_write; long size; char name[260]; };
#define _A_SUBDIR FILE_ATTRIBUTE_DIRECTORY
#define _A_NORMAL 0
#ifdef __cplusplus
extern "C" {
#endif
intptr_t _findfirst(const char *spec, struct _finddata_t *fd);
int      _findnext(intptr_t h, struct _finddata_t *fd);
int      _findclose(intptr_t h);
#ifdef __cplusplus
}
#endif
