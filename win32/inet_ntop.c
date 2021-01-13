#include <winsock2.h>
#include <ws2tcpip.h>

const char *inet_ntop(int af, const void *src, char *dst, size_t size) {
    int ret =
        getnameinfo(src, af == AF_INET ? sizeof(struct in_addr) : sizeof(struct in6_addr), dst, size, NULL, 0, NI_NUMERICHOST);
    if(ret == 0) return dst;
    return NULL;
}
