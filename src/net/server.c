#include <stdint.h>
#include <threads/thread.h>
#include <lwip/inet.h>
#include <lwip/ip_addr.h>
#include <lwip/memp.h>
#include <lwip/netif.h>
#include <lwip/stats.h>
#include <lwip/sys.h>
#include <lwip/tcp_impl.h>
#include <lwip/udp.h>
#include <netif/etharp.h>
#include "net/server.h"
#include "net/timer.h"

struct netif nif;

static struct timer_args timer_arp;
static struct timer_args timer_tcpf;
static struct timer_args timer_tcps;

static void start_timer(struct timer_args *, void (*func)(void), const char *, int64_t);
static void lwip_init(struct netif *, uint32_t, uint32_t, uint32_t);

void
net_init(void)
{
    printf ("Initializing network...");

    stats_init ();
    sys_init ();
    mem_init ();
    memp_init ();
    pbuf_init ();
    etharp_init ();
    ip_init ();
    udp_init ();
    tcp_init ();

    lwip_init (&nif, inet_addr(NET_IP), inet_addr(NET_MASK),
            inet_addr(NET_GATEWAY));

    // set up ARP and TCP timers
    start_timer (&timer_arp, &etharp_tmr, "arp timer", ARP_TMR_INTERVAL);
    start_timer (&timer_tcpf, &tcp_fasttmr, "tcp fast timer", TCP_FAST_INTERVAL);
    start_timer (&timer_tcps, &tcp_slowtmr, "tcp slow timer", TCP_SLOW_INTERVAL);

    struct in_addr ia = {inet_addr(NET_IP)};
    printf ("%02x:%02x:%02x:%02x:%02x:%02x bound to %s.\n",
            nif.hwaddr[0], nif.hwaddr[1], nif.hwaddr[2],
            nif.hwaddr[3], nif.hwaddr[4], nif.hwaddr[5],
            inet_ntoa(ia));
}

err_t
eth_init(struct netif *nif)
{
    nif->mtu = 1500;

    // assign MAC address
    nif->hwaddr[0] = 0x52;
    nif->hwaddr[1] = 0x54;
    nif->hwaddr[2] = 0x00;
    nif->hwaddr[3] = 0xf0;
    nif->hwaddr[4] = 0x95;
    nif->hwaddr[5] = 0x12;

    return ERR_OK;
}

static void
start_timer(struct timer_args *t, void (*func)(void), const char *name, int64_t timeout)
{
    t->timeout = timeout;
    t->func = func;

    //printf ("Starting timer %s\n", name);
    thread_create (name, 0, &timer, t);
}

static void
lwip_init(struct netif *nif, uint32_t inet_addr, uint32_t inet_mask,
        uint32_t inet_gw)
{
    struct ip_addr ipaddr, netmask, gateway;
    ipaddr.addr = inet_addr;
    netmask.addr = inet_mask;
    gateway.addr = inet_gw;

    if (netif_add (nif, &ipaddr, &netmask, &gateway, NULL, eth_init,
                ethernet_input) == 0) {
        PANIC ("lwip_init: netif_add failed\n");
    }

    netif_set_default (nif);
    netif_set_up (nif);
}
