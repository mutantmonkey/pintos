#include <stdio.h>
#include "devices/e1000.h"
#include "threads/thread.h"
#include "net/input.h"

void
net_input (void)
{
    char buf[RECV_BUF_SIZE];

    for (;;)
    {
        // send unsolicited arp
        e1000_transmit ("\xff\xff\xff\xff\xff\xff\x52\x54\x00\xf0\x95\x12\x08\x06\x00\x01\x08\x00\x06\x04\x00\x01\x52\x54\x00\xf0\x95\x12\x0a\x00\x02\x0f\xff\xff\xff\xff\xff\xff\x0a\x00\x02\x02", 42);

        while (e1000_receive (buf, RECV_BUF_SIZE) <= 0)
            thread_yield ();

        printf ("got packet\n");
        hex_dump (0, buf, 64, false);
    }
}
