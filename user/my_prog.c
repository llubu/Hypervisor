// test user-level fault handler -- alloc pages to fix faults

#include <inc/lib.h>

    void
umain(int argc, char **argv)
{
    cprintf("\n IN MY PROG1 \n");
    int a[64] = {0};
    cprintf("\n IN MY PROG2 \n");
}
