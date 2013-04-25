#ifndef NET_TIMER_H
#define NET_TIMER_H

struct timer_args
{
    int64_t timeout;
    void (*func)(void);
};

void
timer (struct timer_args *);

#endif
