/* (Session 56) net_proto.h: the datagrams' bytes. */

#include "net_proto.h"

#include <string.h>

void net_put16(u8 *p, u16 v)
{
    p[0] = (u8)(v >> 8);
    p[1] = (u8)v;
}

void net_put32(u8 *p, u32 v)
{
    net_put16(p, (u16)(v >> 16));
    net_put16(p + 2, (u16)v);
}

void net_put64(u8 *p, u64 v)
{
    net_put32(p, (u32)(v >> 32));
    net_put32(p + 4, (u32)v);
}

u16 net_get16(const u8 *p)
{
    return (u16)((p[0] << 8) | p[1]);
}

u32 net_get32(const u8 *p)
{
    return ((u32)net_get16(p) << 16) | net_get16(p + 2);
}

u64 net_get64(const u8 *p)
{
    return ((u64)net_get32(p) << 32) | net_get32(p + 4);
}

void net_header(u8 *p, NetType type)
{
    p[0] = 'H';
    p[1] = 'Z';
    p[2] = NET_PROTO_VERSION;
    p[3] = (u8)type;
}

int net_type(const u8 *p, int n)
{
    if (n < NET_HEADER || p[0] != 'H' || p[1] != 'Z'
        || p[2] != NET_PROTO_VERSION) {
        return 0;
    }
    if (p[3] == 0 || p[3] >= NET_TYPES) {
        return 0;
    }
    return p[3];
}

void net_settings_pack(const NetSettings *s, u8 out[NET_SETTINGS_LEN])
{
    memset(out, 0, NET_SETTINGS_LEN);
    out[0] = s->map & 7;
    out[1] = s->difficulty & 3;
    net_put32(out + 2, s->fixes);
    out[NET_SETTINGS_VIEW] = s->view & 1;
}

void net_settings_unpack(const u8 in[NET_SETTINGS_LEN], NetSettings *s)
{
    s->map = in[0] & 7;
    s->difficulty = in[1] & 3;
    s->fixes = net_get32(in + 2);
    s->view = in[NET_SETTINGS_VIEW] & 1;
}

void net_name_clean(const char *in, char *out)
{
    int n = 0;
    for (; in != NULL && *in != '\0' && n < NET_NAME_LEN; in++) {
        char c = *in;
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }
        bool keep = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
                    || c == '-';
        if (!keep) {
            c = ' ';
        }
        if (c == ' ' && (n == 0 || out[n - 1] == ' ')) {
            continue;           /* no leading or doubled spaces */
        }
        out[n++] = c;
    }
    while (n > 0 && out[n - 1] == ' ') {
        n--;
    }
    out[n] = '\0';
    if (n == 0) {
        strcpy(out, "PLAYER");
    }
}

void net_name_put(u8 *p, const char *name)
{
    memset(p, ' ', NET_NAME_LEN);
    size_t n = strlen(name);
    memcpy(p, name, (n > NET_NAME_LEN) ? NET_NAME_LEN : n);
}

void net_name_get(const u8 *p, char *out)
{
    char raw[NET_NAME_LEN + 1];
    memcpy(raw, p, NET_NAME_LEN);
    raw[NET_NAME_LEN] = '\0';
    net_name_clean(raw, out);
}
