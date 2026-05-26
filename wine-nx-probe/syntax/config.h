/* Minimal config header for the standalone Horizon ntdll syntax check. */
#ifndef PACKAGE_VERSION
#define PACKAGE_VERSION "wine-nx"
#endif

/* BSD socket support provided by libnx / devkitA64 newlib. */
#ifdef __SWITCH__
#define HAVE_NETINET_IN_H 1
#define HAVE_NETINET_TCP_H 1
#define HAVE_STRUCT_SOCKADDR_SA_LEN 1
#define HAVE_GETADDRINFO 1
#endif
