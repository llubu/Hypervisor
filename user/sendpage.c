// Test Conversation between parent and child environment
// Contributed by Varun Agrawal at Stony Brook

#include <inc/lib.h>

char *str1 = "hello child environment! how are you?";
char *str2 = "hello parent environment! I'm good.";

#define TEMP_ADDR       0xa00000
#define TEMP_ADDR_CHILD 0xb00000
    void
umain(int argc, char **argv)
{
    envid_t who;

    if ((who = fork()) == 0) {
        // Child
        sys_page_alloc(thisenv->env_id, (void *)TEMP_ADDR_CHILD, PTE_SYSCALL);
        ipc_recv(&who, (void *)TEMP_ADDR_CHILD, 0);
        cprintf("%x got message : %s\n", who, (char *)TEMP_ADDR_CHILD);

        if (0 == strncmp((char *)TEMP_ADDR_CHILD, str1, strlen(str1)))
            cprintf("child received correct message\n");

        memcpy((void *)TEMP_ADDR_CHILD, str2, strlen(str1) + 1);
        ipc_send(who, 0, (void *) TEMP_ADDR_CHILD, PTE_SYSCALL);
        return;
    }
    // Parent
    sys_page_alloc(thisenv->env_id, (void *)TEMP_ADDR, PTE_SYSCALL);
    memcpy((void *)TEMP_ADDR, str1, strlen(str1) + 1);
    ipc_send(who, 0, (void *) TEMP_ADDR, PTE_SYSCALL);
    ipc_recv(&who, (void *)TEMP_ADDR, 0);
    cprintf("%x got message : %s\n", who, (char *)TEMP_ADDR);
    if (0 == strncmp((char *)TEMP_ADDR, str2, strlen(str2)))
        cprintf("parent received correct message\n");
    return;
}
