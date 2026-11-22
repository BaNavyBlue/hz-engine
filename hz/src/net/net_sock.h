#ifndef HZ_NET_SOCK_H
#define HZ_NET_SOCK_H

/* (Session 56) UDP sockets, a monotonic clock and a sleep, the same on
 * Linux, macOS and Windows (Winsock; link ws2_32), for the network game's
 * client (net_client.h) and the server (server/hz_server.c). Every socket
 * is non-blocking: net_recv returns at once when nothing has come. */

#include "../core/hz_types.h"

#include <stddef.h>
#include <stdint.h>

typedef intptr_t NetSock;
#define NET_NO_SOCK ((NetSock)-1)

/* An IPv4 or IPv6 address and port, as the socket calls take it. */
typedef struct {
    u8  bytes[128];         /* a sockaddr_storage */
    int len;
} NetAddr;

/* Winsock's start-up on Windows (once; nothing elsewhere). False: no
 * network. */
bool net_init(void);

/* A UDP socket. port > 0: bound to it on every interface, IPv6 taking
 * IPv4 as well where the system allows, else IPv4 alone (the server's);
 * port 0: unbound, of the family `like` is (the client's, for the
 * server's address). NET_NO_SOCK when it can't be had. */
NetSock net_udp_open(int port, const NetAddr *like);
void    net_close(NetSock s);

/* "host" (a name or an address) and a port to an address: the first the
 * resolver gives. Blocks while a name is looked up. */
bool net_resolve(const char *host, int port, NetAddr *out);

/* One datagram. net_send: false when it didn't go (the network down, a
 * full buffer: UDP's loss either way). net_recv: its length, -1 when
 * nothing is waiting (or an error the next call may not have). */
bool net_send(NetSock s, const NetAddr *to, const void *data, int n);
int  net_recv(NetSock s, NetAddr *from, void *buf, int cap);

/* Until a datagram is waiting or `ms` have passed: true when one is. */
bool net_wait(NetSock s, int ms);

bool net_addr_equal(const NetAddr *a, const NetAddr *b);

/* "1.2.3.4:19890" or "[::1]:19890". */
void net_addr_text(const NetAddr *a, char *out, size_t n);

/* Milliseconds from some fixed moment, never going back. */
u64  net_now_ms(void);
void net_sleep_ms(int ms);

/* (Session 63) The same in microseconds, from the system's fine clock
 * (Windows's GetTickCount64 above moves in 16 ms steps), and a sleep as
 * fine as the system allows (on Windows a high-resolution timer, else
 * Sleep's steps): for the pace's checks (hz_netbot). */
u64  net_now_us(void);
void net_sleep_us(u64 us);

/* 32 bits from the system's random source (or the clock, wanting one). */
u32  net_random32(void);

#endif /* HZ_NET_SOCK_H */
