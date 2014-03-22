// test user-level fault handler -- just exit when we fault

#include <inc/lib.h>

    void
handler(struct UTrapframe *utf)
{
    cprintf("\n IN FAULT DIE HANDLER \n");
    void *addr = (void*)utf->utf_fault_va;
    uint32_t err = utf->utf_err;
    cprintf("i faulted at va %x, err %x\n", addr, err & 7);
    sys_env_destroy(sys_getenvid());
}

    void
umain(int argc, char **argv)
{
    cprintf("\n IN FAULT DIE\n");
    set_pgfault_handler(handler);
    *(int*)0xDeadBeef = 0;
}
