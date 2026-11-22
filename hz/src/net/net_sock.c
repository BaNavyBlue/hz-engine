/* (Session 56) net_sock.h: BSD sockets, or Winsock on Windows. */

#if defined(_WIN32)
#define _CRT_RAND_S             /* rand_s, the CRT's system random */
#else
#define _POSIX_C_SOURCE 200809L
#endif

#include "net_sock.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef int socklen_t;
#define RAW(s) ((SOCKET)(s))
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#define RAW(s) ((int)(s))
#endif

_Static_assert(sizeof(struct sockaddr_storage) <= sizeof(((NetAddr *)0)->bytes),
               "NetAddr holds a sockaddr_storage");

bool net_init(void)
{
#ifdef _WIN32
    static bool started = false;
    if (!started) {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            return false;
        }
        started = true;
    }
#endif
    return true;
}

static bool set_nonblocking(NetSock s)
{
#ifdef _WIN32
    u_long on = 1;
    return ioctlsocket(RAW(s), FIONBIO, &on) == 0;
#else
    int flags = fcntl(RAW(s), F_GETFL, 0);
    return flags >= 0 && fcntl(RAW(s), F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

/* A socket of `family`, non-blocking, or NET_NO_SOCK. */
static NetSock open_family(int family)
{
#ifdef _WIN32
    SOCKET raw = socket(family, SOCK_DGRAM, IPPROTO_UDP);
    if (raw == INVALID_SOCKET) {
        return NET_NO_SOCK;
    }
#else
    int raw = socket(family, SOCK_DGRAM, IPPROTO_UDP);
    if (raw < 0) {
        return NET_NO_SOCK;
    }
#endif
    NetSock s = (NetSock)raw;
    if (!set_nonblocking(s)) {
        net_close(s);
        return NET_NO_SOCK;
    }
    return s;
}

static NetSock open_bound(int port)
{
    /* IPv6 on every interface, IPv4 too where the system lets a v6 socket
     * take it (IPV6_V6ONLY off); else IPv4 alone. */
    NetSock s = open_family(AF_INET6);
    if (s != NET_NO_SOCK) {
        int off = 0;
        setsockopt(RAW(s), IPPROTO_IPV6, IPV6_V6ONLY, (const char *)&off,
                   sizeof off);
        struct sockaddr_in6 a6;
        memset(&a6, 0, sizeof a6);
        a6.sin6_family = AF_INET6;
        a6.sin6_addr = in6addr_any;
        a6.sin6_port = htons((u16)port);
        if (bind(RAW(s), (const struct sockaddr *)&a6, sizeof a6) == 0) {
            return s;
        }
        net_close(s);
    }
    s = open_family(AF_INET);
    if (s == NET_NO_SOCK) {
        return NET_NO_SOCK;
    }
    struct sockaddr_in a4;
    memset(&a4, 0, sizeof a4);
    a4.sin_family = AF_INET;
    a4.sin_addr.s_addr = htonl(INADDR_ANY);
    a4.sin_port = htons((u16)port);
    if (bind(RAW(s), (const struct sockaddr *)&a4, sizeof a4) != 0) {
        net_close(s);
        return NET_NO_SOCK;
    }
    return s;
}

NetSock net_udp_open(int port, const NetAddr *like)
{
    if (!net_init()) {
        return NET_NO_SOCK;
    }
    if (port > 0) {
        return open_bound(port);
    }
    int family = AF_INET;
    if (like != NULL) {
        family = ((const struct sockaddr *)like->bytes)->sa_family;
    }
    return open_family(family);
}

void net_close(NetSock s)
{
    if (s == NET_NO_SOCK) {
        return;
    }
#ifdef _WIN32
    closesocket(RAW(s));
#else
    close(RAW(s));
#endif
}

bool net_resolve(const char *host, int port, NetAddr *out)
{
    if (!net_init() || host == NULL || host[0] == '\0') {
        return false;
    }
    char service[8];
    snprintf(service, sizeof service, "%d", port);
    struct addrinfo hints;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    struct addrinfo *found = NULL;
    if (getaddrinfo(host, service, &hints, &found) != 0 || found == NULL) {
        return false;
    }
    memset(out, 0, sizeof *out);
    memcpy(out->bytes, found->ai_addr, (size_t)found->ai_addrlen);
    out->len = (int)found->ai_addrlen;
    freeaddrinfo(found);
    return true;
}

bool net_send(NetSock s, const NetAddr *to, const void *data, int n)
{
    if (s == NET_NO_SOCK) {
        return false;
    }
#ifdef _WIN32
    int sent = sendto(RAW(s), (const char *)data, n, 0,
                      (const struct sockaddr *)to->bytes, to->len);
#else
    ssize_t sent = sendto(RAW(s), data, (size_t)n, 0,
                          (const struct sockaddr *)to->bytes,
                          (socklen_t)to->len);
#endif
    return sent == n;
}

int net_recv(NetSock s, NetAddr *from, void *buf, int cap)
{
    if (s == NET_NO_SOCK) {
        return -1;
    }
    for (;;) {
        socklen_t len = (socklen_t)sizeof from->bytes;
#ifdef _WIN32
        int n = recvfrom(RAW(s), (char *)buf, cap, 0,
                         (struct sockaddr *)from->bytes, &len);
        if (n < 0) {
            /* A port unreachable from an earlier send comes back as
             * WSAECONNRESET on the next read: not this datagram's. */
            if (WSAGetLastError() == WSAECONNRESET) {
                continue;
            }
            return -1;
        }
#else
        ssize_t n = recvfrom(RAW(s), buf, (size_t)cap, 0,
                             (struct sockaddr *)from->bytes, &len);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
#endif
        from->len = (int)len;
        return (int)n;
    }
}

bool net_wait(NetSock s, int ms)
{
    if (s == NET_NO_SOCK) {
        net_sleep_ms(ms);
        return false;
    }
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(RAW(s), &readable);
    struct timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    /* Windows ignores select's first argument. */
    return select((int)RAW(s) + 1, &readable, NULL, NULL, &tv) > 0;
}

bool net_addr_equal(const NetAddr *a, const NetAddr *b)
{
    const struct sockaddr *sa = (const struct sockaddr *)a->bytes;
    const struct sockaddr *sb = (const struct sockaddr *)b->bytes;
    if (sa->sa_family != sb->sa_family) {
        return false;
    }
    if (sa->sa_family == AF_INET) {
        const struct sockaddr_in *x = (const struct sockaddr_in *)a->bytes;
        const struct sockaddr_in *y = (const struct sockaddr_in *)b->bytes;
        return x->sin_port == y->sin_port
               && x->sin_addr.s_addr == y->sin_addr.s_addr;
    }
    if (sa->sa_family == AF_INET6) {
        const struct sockaddr_in6 *x = (const struct sockaddr_in6 *)a->bytes;
        const struct sockaddr_in6 *y = (const struct sockaddr_in6 *)b->bytes;
        return x->sin6_port == y->sin6_port
               && memcmp(&x->sin6_addr, &y->sin6_addr, 16) == 0;
    }
    return false;
}

void net_addr_text(const NetAddr *a, char *out, size_t n)
{
    const struct sockaddr *sa = (const struct sockaddr *)a->bytes;
    char host[INET6_ADDRSTRLEN] = "?";
    int port = 0;
    if (sa->sa_family == AF_INET) {
        const struct sockaddr_in *x = (const struct sockaddr_in *)a->bytes;
        inet_ntop(AF_INET, &x->sin_addr, host, sizeof host);
        port = ntohs(x->sin_port);
        snprintf(out, n, "%s:%d", host, port);
    } else if (sa->sa_family == AF_INET6) {
        const struct sockaddr_in6 *x = (const struct sockaddr_in6 *)a->bytes;
        inet_ntop(AF_INET6, &x->sin6_addr, host, sizeof host);
        port = ntohs(x->sin6_port);
        snprintf(out, n, "[%s]:%d", host, port);
    } else {
        snprintf(out, n, "?");
    }
}

u64 net_now_ms(void)
{
#ifdef _WIN32
    return (u64)GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (u64)ts.tv_sec * 1000u + (u64)ts.tv_nsec / 1000000u;
#endif
}

void net_sleep_ms(int ms)
{
    if (ms <= 0) {
        return;
    }
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
#endif
}

u64 net_now_us(void)
{
#ifdef _WIN32
    static LARGE_INTEGER freq;
    if (freq.QuadPart == 0) {
        QueryPerformanceFrequency(&freq);
    }
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (u64)(t.QuadPart / freq.QuadPart) * 1000000u
           + (u64)(t.QuadPart % freq.QuadPart) * 1000000u
             / (u64)freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (u64)ts.tv_sec * 1000000u + (u64)ts.tv_nsec / 1000u;
#endif
}

#if defined(_WIN32) && !defined(CREATE_WAITABLE_TIMER_HIGH_RESOLUTION)
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

void net_sleep_us(u64 us)
{
    if (us == 0) {
        return;
    }
#ifdef _WIN32
    /* Windows 10 1803 on: a timer to a fraction of a millisecond. */
    static HANDLE timer;
    static bool tried;
    if (!tried) {
        tried = true;
        timer = CreateWaitableTimerExW(NULL, NULL,
                                       CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                       TIMER_ALL_ACCESS);
    }
    LARGE_INTEGER due;
    due.QuadPart = -(LONGLONG)(us * 10u);       /* 100 ns, relative */
    if (timer != NULL && SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE)) {
        WaitForSingleObject(timer, INFINITE);
    } else {
        Sleep((DWORD)((us + 999u) / 1000u));
    }
#else
    struct timespec ts;
    ts.tv_sec = (time_t)(us / 1000000u);
    ts.tv_nsec = (long)(us % 1000000u) * 1000L;
    nanosleep(&ts, NULL);
#endif
}

u32 net_random32(void)
{
    u32 r = 0;
#ifdef _WIN32
    unsigned int w = 0;
    if (rand_s(&w) == 0) {
        return (u32)w;
    }
#else
    FILE *f = fopen("/dev/urandom", "rb");
    if (f != NULL) {
        size_t got = fread(&r, 1, sizeof r, f);
        fclose(f);
        if (got == sizeof r) {
            return r;
        }
    }
#endif
    /* No random source: the clocks, mixed (splitmix64's finish). */
    u64 z = net_now_ms() ^ ((u64)time(NULL) << 20) ^ (u64)(uintptr_t)&r;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return (u32)(z ^ (z >> 31));
}
