/*
 * WLAN / Inet bring-up and local IP helpers.
 */
#ifndef FLSUN_NET_WLAN_H
#define FLSUN_NET_WLAN_H

#ifdef __cplusplus
extern "C" {
#endif

int  net_init(void);
void net_term(void);
int  net_ready(void);
const char *net_local_ip(void);
void net_set_local_ip_override(const char *ip);
void net_refresh_local_ip(const char *route_hint_ip);

#ifdef __cplusplus
}
#endif

#endif /* FLSUN_NET_WLAN_H */
