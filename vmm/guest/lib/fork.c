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
	uint64_t err = utf->utf_err;
	int r;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at vpt
	//   (see <inc/memlayout.h>).
	// LAB 4: Your code here.
	//cprintf("%x, %x\n",addr,utf->utf_rip); //vpt[VPN(addr)]
	if( !(err & FEC_WR) )
		panic("the pagefault didn't occur cos of a write. %x, %d", addr, err); 
	if( !(vpt[VPN(addr)] & PTE_COW) )
		panic("Page not marked COW. It's a real pagefault so die. %x, %d", addr, err);

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.
	//   No need to explicitly delete the old page's mapping.
	// LAB 4: Your code here.
	if((r= sys_page_alloc(0, (void *)PFTEMP, PTE_U|PTE_P|PTE_W)) < 0)
		panic("pagefault handler couldn't alloc new page at PFTEMP %e", r);
		
	memcpy((void *)PFTEMP , ROUNDDOWN(addr,PGSIZE), PGSIZE);
	
	if((r= sys_page_map(0, (void *)PFTEMP, 0, ROUNDDOWN(addr,PGSIZE), PTE_U|PTE_P|PTE_W)) < 0)
		panic("pagefault handler couldn't remap page %e", r);
	
	if((r= sys_page_unmap(0, (void *)PFTEMP)) < 0)
		panic("pagefault handler couldn't dealloc PFTEMP %e", r);
	//panic("pgfault not implemented");

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
	int r;
	pte_t pte = vpt[pn];
	void * addr = (void *)((uint64_t)pn * PGSIZE);	
	int perm = pte & PTE_USER;
	
	if(perm&PTE_SHARE) {
		if((r = sys_page_map(0, addr, envid, addr, perm)) < 0)	//map shared pages
			panic("Couldn't map shared page %e",r);
		return 0;
	}
	else if((perm&PTE_W) || (perm&PTE_COW)) {	//if write or COW pages
		 perm &= ~PTE_W;
   		 perm |= PTE_COW;
		if((r = sys_page_map(0, addr, envid, addr, perm)) < 0)	// map once in child
			panic("Couldn't map page COW child %e",r);
		
		if((r = sys_page_map(0, addr, 0, addr, perm)) < 0) //map once in yourself
			panic("Couldn't map page COW parent %e",r);
			return 0;
	}
	else {
		//cprintf("perm: %x\n",perm);
		if((r = sys_page_map(0, addr, envid, addr, PTE_P|PTE_U)) < 0)	//map readonly pages
			panic("Couldn't map readonly page %e",r);
		return 0;
	}
}

// The basic control flow for fork() is as follows:

// 		The parent installs pgfault() as the C-level page fault handler, using the set_pgfault_handler() function you implemented above.
//		 The parent calls sys_exofork() to create a child environment.
//		 For each writable or copy-on-write page in its address space below UTOP, the parent calls duppage, which should map the page copy-on-write into the address space of the child and then remap the page copy-on-write in its own address space. duppage sets both PTEs so that the page is not writeable, and to contain PTE_COW in the "avail" field to distinguish copy-on-write pages from genuine read-only pages.
//		 The exception stack is not remapped this way, however. Instead you need to allocate a fresh page in the child for the exception stack. 
// 		Since the page fault handler will be doing the actual copying and the page fault handler runs on the exception stack, the exception stack cannot be made copy-on-write: who would copy it?
// 		fork() also needs to handle pages that are present, but not writable or copy-on-write.
// 		The parent sets the user page fault entrypoint for the child to look like its own.
// 		The child is now ready to run, so the parent marks it runnable.
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
	set_pgfault_handler(pgfault);
	envid_t eid = sys_exofork();
	if(eid < 0) // error
		panic("couldn't fork child. %e",eid);
	else if(eid == 0) { // child
		thisenv = envs+ENVX(sys_getenvid());
		return 0;
	}
	else { //parent
		uint64_t r;
    		for (r = 0; r < (uint64_t)(UXSTACKTOP-PGSIZE); r += PGSIZE) {
    			if(	(vpml4e[VPML4E(r)] & PTE_P) &&
    				(vpde[VPDPE(r)] & PTE_P) &&
 	   			(vpd[VPD(r)] & PTE_P) && 
 	   			(vpt[VPN(r)] & PTE_P)) 
 	   			{	
					duppage(eid, VPN(r));
				}
			}
		
		//Duppage the stack for the child
		duppage(eid, VPN(USTACKTOP-PGSIZE));
		
		// Instead you need to allocate a fresh page in the child for the exception stack
		if((r= sys_page_alloc(eid, (void *)(UXSTACKTOP-PGSIZE), PTE_U|PTE_P|PTE_W)) < 0)
			panic("fork failed setting UXSTACK for child %e", r);
		
		//The parent sets the user page fault entrypoint for the child to look like its own.
		if((r= sys_env_set_pgfault_upcall(eid, thisenv->env_pgfault_upcall)) < 0) //
			panic("fork failed setting pgfault handler %e", r);
		
		//The child is now ready to run, so the parent marks it runnable.
		if((r= sys_env_set_status(eid, ENV_RUNNABLE)) < 0)
			panic("fork failed setting status %e", r);
		//cprintf("forked the child\n");
		return eid;
	}
	//panic("fork not implemented");
}

// Challenge!
    int
sfork(void)
{
    panic("sfork not implemented");
    return -E_INVAL;
}
