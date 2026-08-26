#pragma once
#include "windows.h"
// ODBC is server-side only; the client never links it. Types kept so headers parse.
typedef void *SQLHANDLE, *SQLHENV, *SQLHDBC, *SQLHSTMT;
typedef short SQLSMALLINT, SQLRETURN;
typedef unsigned char SQLCHAR;
typedef long SQLINTEGER;
typedef unsigned long SQLUINTEGER;
typedef SQLINTEGER *SQLLEN, *SQLPOINTER;
#define SQL_SUCCESS 0
#define SQL_SUCCESS_WITH_INFO 1
#define SQL_ERROR (-1)
#define SQL_NTS (-3)
typedef struct tagTIMESTAMP_STRUCT {
    SQLSMALLINT year; unsigned short month, day, hour, minute, second; SQLUINTEGER fraction;
} TIMESTAMP_STRUCT;
typedef struct tagDATE_STRUCT { SQLSMALLINT year; unsigned short month, day; } DATE_STRUCT;
typedef struct tagTIME_STRUCT { unsigned short hour, minute, second; } TIME_STRUCT;
typedef double SQLFLOAT;
typedef double SQLDOUBLE;
typedef float  SQLREAL;
