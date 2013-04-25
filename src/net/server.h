#ifndef NET_SERVER_H
#define NET_SERVER_H

#include <lwip/netif.h>

#define NET_IP "10.177.0.10"
#define NET_MASK "255.255.255.0"
#define NET_GATEWAY "10.177.0.1"

void net_init (void);
err_t eth_init (struct netif *);

#endif
