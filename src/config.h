#ifndef __SN_CONFIG_H
#define __SN_CONFIG_H

#ifdef __APPLE__
#include <AvailabilityMacros.h>
#endif

/* test for polling API */
#ifdef __linux__
#define HAVE_EPOLL 1
#endif


#if (defined(__APPLE__) && defined(MAC_OS_X_VERSION_10_6)) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined (__NetBSD__)
#define HAVE_KQUEUE 1
#endif

#endif