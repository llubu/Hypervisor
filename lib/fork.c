// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
    static void
pgfault(struct UTrapframe *utf)
{
    void *addr = (void *) utf->utf_fault_va;
    uint32_t err = utf->utf_err;
    int r;

    // Check that the faulting access was (1) a write, and (2) to a
    // copy-on-write page.  If not, panic.
    // Hint:
    //   Use the read-only page table mappings at vpt
    //   (see <inc/memlayout.h>).

    // LAB 4: Your code here.
    pte_t pte = vpt[((uint64_t) addr / PGSIZE)];

    if (!(err & FEC_WR))
	panic("Faulting access write(FEC_WR) failed = 0x%x: err %x\n\n", (uint64_t) addr, err);

    if (!(pte & PTE_COW))
	panic("Faulting access copy-on-write(PTE_COW) failed = 0x%x, env_id 0x%x\n", (uint64_t) addr, thisenv->env_id);


    // Allocate a new page, map it at a temporary location (PFTEMP),
    // copy the data from the old page to the new page, then move the new
    // page to the old page's address.
    // Hint:
    //   You should make three system calls.
    //   No need to explicitly delete the old page's mapping.

    // LAB 4: Your code here.
    if ((r = sys_page_alloc(0, (void *)PFTEMP, PTE_P|PTE_U|PTE_W)) < 0)
	panic("sys_page_alloc failed: %e\n", r);
    memcpy(PFTEMP, ROUNDDOWN(addr, PGSIZE), PGSIZE);

    void *vaTemp = (void *) ROUNDDOWN((uint64_t) addr, PGSIZE);
    
    if ((r = sys_page_map(0, (void *)PFTEMP, 0, vaTemp, PTE_P|PTE_U|PTE_W)) < 0)
    {
	panic("sys_page_map failed: %e\n", r);
    }
    if ((r = sys_page_unmap(0, (void *)PFTEMP)) < 0)
    {
	panic("sys_page_unmap failed: %e\n", r);
    }
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
    static int
duppage(envid_t envid, unsigned pn)
{
    // LAB 4: Your code here.
    unsigned va;
    int r;
    pte_t pte = vpt[pn];
    int perm = pte & 0xfff;
    int perm_share = pte & PTE_USER;
	
    va = pn * PGSIZE;

    if (perm_share & PTE_SHARE)
	return sys_page_map(0, (void *)(uint64_t)va, envid, (void *)(uint64_t)va, perm_share);

    if (pte & (PTE_W|PTE_COW)) 
    {
	//child
	if ((r = sys_page_map(0, (void *)(uint64_t)va, envid, (void *)(uint64_t)va, PTE_P|PTE_U|PTE_COW)) < 0)
	    panic("sys_page_map failed: %e\n", r);

	// parent
	return sys_page_map(0, (void *)(uint64_t)va, 0, (void *)(uint64_t)va, PTE_P|PTE_U|PTE_COW);
    }

    return sys_page_map(0, (void *)(uint64_t)va, envid, (void *)(uint64_t)va, PTE_P|PTE_U|perm);
}

extern void _pgfault_upcall(void);
//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use vpd, vpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
    envid_t
fork(void)
{
    // LAB 4: Your code here.
    envid_t envid;
    uint64_t addr;
    uint32_t err;
    extern unsigned char end[];
    int r;

    set_pgfault_handler(pgfault);
    envid = sys_exofork();


    if (envid < 0)
                panic("sys_exofork: %e", envid);
    if (envid == 0) {
	// We're the child.
        // The copied value of the global variable 'thisenv'
        // is no longer valid (it refers to the parent!).
        // Fix it and return 0.

        thisenv = &envs[ENVX(sys_getenvid())];
        return 0;
     }

     //Allocate exception stack for the child
     if ((err = sys_page_alloc(envid, (void *) (UXSTACKTOP - PGSIZE), PTE_P|PTE_U|PTE_W)) < 0)
	 panic("Error in sys_page_alloc: %e", err);

        // We're the parent.
        // Map our entire address space into the child.
        for (addr = UTEXT; addr < USTACKTOP-PGSIZE; addr += PGSIZE) {
               if((vpml4e[VPML4E(addr)] & PTE_P) && (vpde[VPDPE(addr)] & PTE_P) 
			&& (vpd[VPD(addr)] & PTE_P) && (vpt[VPN(addr)] & PTE_P)) {
                       duppage(envid, VPN(addr));
		}
	}

	//Allocate a new stack for the child and copy the contents of parent on to it.
	addr = USTACKTOP-PGSIZE;
        if ((r = sys_page_alloc(0, (void *)PFTEMP, PTE_P|PTE_U|PTE_W)) < 0)
                panic("sys_page_alloc failed: %e\n", r);
        memcpy(PFTEMP, (void *) ROUNDDOWN(addr, PGSIZE), PGSIZE);
        void *vaTemp = (void *) ROUNDDOWN(addr, PGSIZE);
        if ((r = sys_page_map(0, (void *)PFTEMP, envid, vaTemp, PTE_P|PTE_U|PTE_W)) < 0)
                panic("sys_page_map failed: %e\n", r);
        if ((r = sys_page_unmap(0, (void *)PFTEMP)) < 0)
                panic("sys_page_unmap failed: %e\n", r);

	//Set child's page fault handler
	if ((err = sys_env_set_pgfault_upcall(envid, _pgfault_upcall) < 0))
		panic("Error in sys_env_set_pgfault_upcall: %e",err);

	//Set the child ready to run
        if ((err = sys_env_set_status(envid, ENV_RUNNABLE)) < 0)
                panic("sys_env_set_status: %e", err);

	return envid;

}

// Challenge!
    int
sfork(void)
{
    panic("sfork not implemented");
    return -E_INVAL;
}
