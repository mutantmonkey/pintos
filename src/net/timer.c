#include "devices/timer.h"
#include "net/timer.h"
    
void
timer (struct timer_args *t)
{
    for (;;)
    {
        timer_msleep (t->timeout);
        t->func ();
    }
}
