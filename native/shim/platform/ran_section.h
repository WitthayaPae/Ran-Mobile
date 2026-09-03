//  Scoped section timing, for the frame-section report.
//
//  GLGaeaClient.cpp grew this first and kept it to itself, which meant any
//  question inside one of its sections - "which half of w:chars is the cost?" -
//  could not be asked without copying the struct. It lives here now so any
//  mobile-only block can be timed the same way.
//
//  The names come out in the "FRAME sections:" line, sorted by cost. A section
//  costs two clock reads, so it is meant for blocks measured in milliseconds,
//  not for inner loops.
#pragma once
#ifdef RAN_MOBILE

#include <time.h>

#ifdef __cplusplus
extern "C" void RanProf_Section ( const char *szName, double fSeconds );

namespace RanProf
{
	struct Section
	{
		const char     *m_szName;
		struct timespec m_start;
		explicit Section ( const char *szName ) : m_szName ( szName )
		{ clock_gettime ( CLOCK_MONOTONIC, &m_start ); }
		~Section ()
		{
			struct timespec now;
			clock_gettime ( CLOCK_MONOTONIC, &now );
			RanProf_Section ( m_szName,
				(double)( now.tv_sec  - m_start.tv_sec  ) +
				(double)( now.tv_nsec - m_start.tv_nsec ) * 1e-9 );
		}
	};
}

#define RAN_SECTION(name) RanProf::Section ranSection__ ( name )
#endif  //  __cplusplus

#else
#define RAN_SECTION(name) ((void)0)
#endif  //  RAN_MOBILE
