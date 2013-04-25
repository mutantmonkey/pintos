#include <stdint.h>
#include <lwip/ip_addr.h>
#include <lwip/netif.h>
#include <lwip/inet.h>
#include <netif/etharp.h>
#include "net/server.h"

struct netif nif;

static void lwip_init(struct netif *nif, uint32_t inet_addr,
        uint32_t inet_mask, uint32_t inet_gw);

void
net_init(void)
{
    printf ("Initializing network...\n");

    //tcpip_init (&tcpip_init_done, &done);
    lwip_init (&nif, inet_addr(NET_IP), inet_addr(NET_MASK),
            inet_addr(NET_GATEWAY));

    // TODO: setup ARP and TCP timers

    struct in_addr ia = {inet_addr(NET_IP)};
    printf ("%02x:%02x:%02x:%02x:%02x:%02x bound to %s\n",
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
