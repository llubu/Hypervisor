#include <inc/lib.h>
#include <inc/x86.h>

    void
sleep(int sec)
{
    unsigned chk = 0;
    unsigned now = sys_time_msec();
//    unsigned end = now + sec * 1000;
    unsigned end = now + sec * 100;
    if ((int)now < 0 && (int)now > -MAXERROR)
        panic("sys_time_msec: %e", (int)now);
    if (end < now)
        panic("sleep: wrap");

    while ((chk = sys_time_msec()) < end)
    {
//	cprintf("IN TEST TIME END-WHILE IS :%u::%u\n", end, chk);
        sys_yield();
    }
}

    void
umain(int argc, char **argv)
{
    int i;

    // Wait for the console to calm down
    for (i = 0; i < 50; i++)
        sys_yield();

    cprintf("starting count down: ");
    for (i = 5; i >= 0; i--) {
        cprintf("%d ", i);
        sleep(1);
    }
    cprintf("\n");
    breakpoint();
}
