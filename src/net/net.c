/*
 * WLAN / Inet init and LAN IP detection (Apctl + UDP getsockname route trick).
 */
#include "net.h"
#include "httpd.h"
#include "socket.h"
#include "model_storage.h"
#include "config.h"
#include "worker.h"
#include "log.h"

#include <pspkernel.h>
#include <pspsdk.h>
#include <psputility.h>
#include <psputility_netmodules.h>
#include <pspnet.h>
#include <pspnet_inet.h>
#include <pspnet_apctl.h>
#include <pspwlan.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int g_net_ok;
static char g_local_ip[16] = "0.0.0.0";

static int ip_is_lan_displayable(const char *ip)
{
	unsigned char oct[4];

	if (!ip || !ip[0])
		return 0;
	if (sock_parse_ipv4(ip, oct) != 0)
		return 0;
	if (oct[0] == 0 || oct[0] == 127)
		return 0;
	return 1;
}

static int lan_ip_via_route(const char *peer_ip, char *out, int outlen)
{
	int fd;
	struct sockaddr_in dest;
	struct sockaddr_in local;
	socklen_t llen;
	unsigned char oct[4];
	char buf[16];

	if (!peer_ip || sock_parse_ipv4(peer_ip, oct) != 0)
		return -1;

	fd = sceNetInetSocket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0)
		return -1;

	memset(&dest, 0, sizeof(dest));
	dest.sin_len = sizeof(dest);
	dest.sin_family = AF_INET;
	dest.sin_port = htons(9);
	dest.sin_addr.s_addr = htonl(((unsigned)oct[0] << 24) |
				     ((unsigned)oct[1] << 16) |
				     ((unsigned)oct[2] << 8) |
				     (unsigned)oct[3]);

	if (sceNetInetConnect(fd, (struct sockaddr *)&dest, sizeof(dest)) < 0) {
		sceNetInetClose(fd);
		return -1;
	}

	memset(&local, 0, sizeof(local));
	llen = sizeof(local);
	if (sceNetInetGetsockname(fd, (struct sockaddr *)&local, &llen) < 0) {
		sceNetInetClose(fd);
		return -1;
	}
	sceNetInetClose(fd);

	{
		unsigned int a = ntohl(local.sin_addr.s_addr);
		snprintf(buf, sizeof(buf), "%u.%u.%u.%u",
			 (a >> 24) & 255, (a >> 16) & 255,
			 (a >> 8) & 255, a & 255);
	}
	if (!ip_is_lan_displayable(buf))
		return -1;
	snprintf(out, outlen, "%s", buf);
	return 0;
}

void net_refresh_local_ip(const char *route_hint_ip)
{
	union SceNetApctlInfo info;
	char found[16];

	found[0] = 0;

	if (route_hint_ip && route_hint_ip[0] &&
	    lan_ip_via_route(route_hint_ip, found, sizeof(found)) == 0)
		goto apply;

	if (lan_ip_via_route("1.1.1.1", found, sizeof(found)) == 0)
		goto apply;
	if (lan_ip_via_route("8.8.8.8", found, sizeof(found)) == 0)
		goto apply;

	memset(&info, 0, sizeof(info));
	if (sceNetApctlGetInfo(PSP_NET_APCTL_INFO_IP, &info) == 0 &&
	    ip_is_lan_displayable(info.ip)) {
		snprintf(found, sizeof(found), "%s", info.ip);
		goto apply;
	}

	if (ip_is_lan_displayable(g_local_ip))
		return;
	strcpy(g_local_ip, "0.0.0.0");
	return;

apply:
	snprintf(g_local_ip, sizeof(g_local_ip), "%s", found);
	httpd_note_local_ip(g_local_ip);
}

int net_init(void)
{
	int err;

	if (sock_runtime_init()!=0) return -1;
	g_net_ok = 0;
	strcpy(g_local_ip, "0.0.0.0");

	sceUtilityLoadNetModule(PSP_NET_MODULE_COMMON);
	sceUtilityLoadNetModule(PSP_NET_MODULE_INET);

	err = pspSdkInetInit();
	if (err != 0) {
		err = sceNetInit(0x20000, 0x20, 0x1000, 0x20, 0x1000);
		if (err == 0)
			err = sceNetInetInit();
		if (err == 0)
			err = sceNetApctlInit(0x1800, 0x30);
		if (err != 0)
			return err;
	}

	(void)sceNetApctlConnect(1);
	sceKernelDelayThread(200 * 1000);

	net_refresh_local_ip(NULL);

	g_net_ok = 1;
	cfg_ensure_dirs();
	model_storage_init();
	return 0;
}

void net_set_local_ip_override(const char *ip)
{
	if (!ip || !ip[0])
		return;
	if (!ip_is_lan_displayable(ip))
		return;
	snprintf(g_local_ip, sizeof(g_local_ip), "%s", ip);
}

void net_term(void)
{
	sock_runtime_shutdown();
    httpd_shutdown();
    net_worker_stop();
    log_term();
	sceNetApctlDisconnect();
	pspSdkInetTerm();
	g_net_ok = 0;
}

int net_ready(void)
{
	return g_net_ok;
}

const char *net_local_ip(void)
{
	return g_local_ip;
}
