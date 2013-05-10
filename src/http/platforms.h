/*****************************************************************************
*    Open LiteSpeed is an open source HTTP server.                           *
*    Copyright (C) 2013  LiteSpeed Technologies, Inc.                        *
*                                                                            *
*    This program is free software: you can redistribute it and/or modify    *
*    it under the terms of the GNU General Public License as published by    *
*    the Free Software Foundation, either version 3 of the License, or       *
*    (at your option) any later version.                                     *
*                                                                            *
*    This program is distributed in the hope that it will be useful,         *
*    but WITHOUT ANY WARRANTY; without even the implied warranty of          *
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the            *
*    GNU General Public License for more details.                            *
*                                                                            *
*    You should have received a copy of the GNU General Public License       *
*    along with this program. If not, see http://www.gnu.org/licenses/.      *
*****************************************************************************/
#ifndef _HTTP_PLATFORM_
#define _HTTP_PLATFORM_


#if defined(__i386__)
#if defined(linux) || defined(__linux) || defined(__linux__)
#define LS_PLATFORM "i386-linux"
#elif defined(__FreeBSD__) 
#if __FreeBSD__ == 6
#define LS_PLATFORM "i386-freebsd6"
#else
#define LS_PLATFORM "i386-freebsd"
#endif
#elif defined(__NetBSD__) 
#define LS_PLATFORM "i386-netbsd"

#elif defined(__OpenBSD__)
#define LS_PLATFORM "i386-openbsd"

#elif defined(sun) || defined(__sun)
#define LS_PLATFORM "i386-solaris"

#elif defined(macintosh) || defined(__APPLE__) || defined(__APPLE_CC__)
#define LS_PLATFORM "i386-osx"

#else
#define LS_PLATFORM "i386-unknown"

#endif

#elif defined( __x86_64 )||defined( __x86_64__ )

#if defined(linux) || defined(__linux) || defined(__linux__)
#define LS_PLATFORM "x86_64-linux"
#elif defined(__FreeBSD__) 
#define LS_PLATFORM "x86_64-freebsd6"
#elif defined(__NetBSD__) 
#define LS_PLATFORM "x86_64-netbsd"

#elif defined(__OpenBSD__)
#define LS_PLATFORM "x86_64-openbsd"

#elif defined(sun) || defined(__sun)
#define LS_PLATFORM "x86_64-solaris"

#else
#define LS_PLATFORM "x86_64-unknown"

#endif


#elif defined(__sparc) || defined(__sparc__) 
#if defined(sun) || defined(__sun)
#define LS_PLATFORM "sparc-solaris"

#else
#define LS_PLATFORM "sparc-unknown"
#endif



#elif defined(__powerpc__) || defined(__ppc__)|| defined(_POWER)
#if defined(linux) || defined(__linux) || defined(__linux__)
#define LS_PLATFORM "ppc-linux"

#elif defined(macintosh) || defined(__APPLE__) || defined(__APPLE_CC__)
#define LS_PLATFORM "ppc-osx"

#else
#define LS_PLATFORM "ppc-unknown"
#endif



#else
#define LS_PLATFORM "unknown-unknown"

#endif


#endif
