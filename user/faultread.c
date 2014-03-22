// buggy program - faults with a read from location zero

#include <inc/lib.h>

    void
umain(int argc, char **argv)
{
    cprintf("\n IN FAULT READ \n");
    cprintf("I read %08x from location 0!\n", *(unsigned*)0);
}

