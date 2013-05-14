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
        while (e1000_receive (buf, RECV_BUF_SIZE) <= 0)
            thread_yield ();

        printf ("got packet\n");
        hex_dump (0, buf, 64, false);
    }
}
