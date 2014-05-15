/* See COPYRIGHT for copyright information. */

#include <inc/x86.h>
#include <inc/error.h>
#include <inc/string.h>
#include <inc/assert.h>
#include <inc/fs.h>

#include <kern/env.h>
#include <kern/pmap.h>
#include <kern/trap.h>
#include <kern/syscall.h>
#include <kern/console.h>
#include <kern/sched.h>
#include <kern/time.h>
#include <kern/e1000.h>
#include <vmm/ept.h>

// Print a string to the system console.
// The string is exactly 'len' characters long.
// Destroys the environment on memory errors.
    static void
sys_cputs(const char *s, size_t len)
{
    // Check that the user has permission to read memory [s, s+len).
    // Destroy the environment if not.

    // LAB 3: Your code here.
    user_mem_assert(curenv, (void*)s, len, PTE_P | PTE_U);

    // Print the string supplied by the user.
    cprintf("%.*s", len, s);
}

// Read a character from the system console without blocking.
// Returns the character, or 0 if there is no input waiting.
    static int
sys_cgetc(void)
{
    return cons_getc();
}

// Returns the current environment's envid.
    static envid_t
sys_getenvid(void)
{
    return curenv->env_id;
}

// Destroy a given environment (possibly the currently running environment).
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
    static int
sys_env_destroy(envid_t envid)
{
    int r;
    struct Env *e;

    if ((r = envid2env(envid, &e, 1)) < 0)
        return r;
    if (e == curenv)
	cprintf("[%08x] exiting gracefully\n", curenv->env_id);
    else
	cprintf("[%08x] destroying %08x\n", curenv->env_id, e->env_id);

    env_destroy(e);
    return 0;
}

// Deschedule current environment and pick a different one to run.
    static void
sys_yield(void)
{
    sched_yield();
}

// Allocate a new environment.
// Returns envid of new environment, or < 0 on error.  Errors are:
//	-E_NO_FREE_ENV if no free environment is available.
//	-E_NO_MEM on memory exhaustion.
    static envid_t
sys_exofork(void)
{
    // Create the new environment with env_alloc(), from kern/env.c.
    // It should be left as env_alloc created it, except that
    // status is set to ENV_NOT_RUNNABLE, and the register set is copied
    // from the current environment -- but tweaked so sys_exofork
    // will appear to return 0.

    // LAB 4: Your code here.
    struct Env *env;
    int err = env_alloc(&env, curenv->env_id);
    if (err < 0)
    {
	return err;
    }
    else if (!err)
    {
	env->env_status = ENV_NOT_RUNNABLE;
	memcpy(&(env->env_tf), &(curenv->env_tf), sizeof(struct Trapframe));

	// Parent is going to set child's status to RUNNING at some point of
	// time. On this occurance, child will expect to have the error code
	// in ax register. Thus, directly put value 0 in ax, so that child
	// reads it whe needed.
	env->env_tf.tf_regs.reg_rax = 0;

	return env->env_id; // Parent should return child's envid
     }
    panic("sys_exofork not implemented");
}

// Set envid's env_status to status, which must be ENV_RUNNABLE
// or ENV_NOT_RUNNABLE.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if status is not a valid status for an environment.
    static int
sys_env_set_status(envid_t envid, int status)
{
    // Hint: Use the 'envid2env' function from kern/env.c to translate an
    // envid to a struct Env.
    // You should set envid2env's third argument to 1, which will
    // check whether the current environment has permission to set
    // envid's status.

    // LAB 4: Your code here.
    if (status != ENV_RUNNABLE && status != ENV_NOT_RUNNABLE)
    {
	return -E_INVAL;
    }

    struct Env *env;
    int err = envid2env(envid, &env, 1);
    
    if (err < 0)
    {
	return err;
    }
    else if (err == 0)
    {
	env->env_status = status;
	cprintf(":%d\n",__LINE__);
	return 0;
   }
    panic("sys_env_set_status not implemented");
}

// Set envid's trap frame to 'tf'.
// tf is modified to make sure that user environments always run at code
// protection level 3 (CPL 3) with interrupts enabled.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
    static int
sys_env_set_trapframe(envid_t envid, struct Trapframe *tf)
{
    // LAB 5: Your code here.
    // Remember to check whether the user has supplied us with a good
    // address!
    struct Env *env;
    int err = envid2env(envid, &env, 1);
    if (err < 0)
	return err;
    else if (err == 0) {
	user_mem_assert(env, tf, sizeof(struct Trapframe),PTE_P|PTE_U);
	env->env_tf = *tf;
	return 0;
    }
    panic("sys_env_set_trapframe not implemented");
}

// Set the page fault upcall for 'envid' by modifying the corresponding struct
// Env's 'env_pgfault_upcall' field.  When 'envid' causes a page fault, the
// kernel will push a fault record onto the exception stack, then branch to
// 'func'.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
    static int
sys_env_set_pgfault_upcall(envid_t envid, void *func)
{
    // LAB 4: Your code here.
    struct Env *env;
    int err = envid2env(envid, &env, 1);
//    cprintf("\n IN SYSCALL: set PFH:%d:%x:\n", err, func);
    if (err < 0)
    {
	return err;
    }
    else if (err == 0)
    {
	user_mem_assert(env, func, sizeof(func), PTE_P|PTE_U);
	env->env_pgfault_upcall = func;
	return 0;
    }
    panic("sys_env_set_pgfault_upcall not implemented");
}

// Allocate a page of memory and map it at 'va' with permission
// 'perm' in the address space of 'envid'.
// The page's contents are set to 0.
// If a page is already mapped at 'va', that page is unmapped as a
// side effect.
//
// perm -- PTE_U | PTE_P must be set, PTE_AVAIL | PTE_W may or may not be set,
//         but no other bits may be set.  See PTE_SYSCALL in inc/mmu.h.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if va >= UTOP, or va is not page-aligned.
//	-E_INVAL if perm is inappropriate (see above).
//	-E_NO_MEM if there's no memory to allocate the new page,
//		or to allocate any necessary page tables.
    static int
sys_page_alloc(envid_t envid, void *va, int perm)
{
    // Hint: This function is a wrapper around page_alloc() and
    //   page_insert() from kern/pmap.c.
    //   Most of the new code you write should be to check the
    //   parameters for correctness.
    //   If page_insert() fails, remember to free the page you
    //   allocated!

    // LAB 4: Your code here.
    if ((uint64_t)va >= UTOP || va != ROUNDUP(va, PGSIZE))
    {
	return -E_INVAL;
    }
    if (!(perm & PTE_U) || !(perm & PTE_P))
    {
	return -E_INVAL;
    }

    struct Page *p = NULL;
    if (!(p = page_alloc(ALLOC_ZERO)))
    {
	return -E_NO_MEM;
    }

    struct Env *env;
    int err = envid2env(envid, &env, 1);
    if (err < 0)
    {
	return err;
    }
    else if (err == 0)
    {
	page_remove(env->env_pml4e, va);		
	if (page_insert(env->env_pml4e, p, va, perm) < 0)
	{
	    page_free(p);
	    return -E_NO_MEM;
	}
                return 0;
     }

    panic("sys_page_alloc not implemented");
}

// Map the page of memory at 'srcva' in srcenvid's address space
// at 'dstva' in dstenvid's address space with permission 'perm'.
// Perm has the same restrictions as in sys_page_alloc, except
// that it also must not grant write access to a read-only
// page.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if srcenvid and/or dstenvid doesn't currently exist,
//		or the caller doesn't have permission to change one of them.
//	-E_INVAL if srcva >= UTOP or srcva is not page-aligned,
//		or dstva >= UTOP or dstva is not page-aligned.
//	-E_INVAL is srcva is not mapped in srcenvid's address space.
//	-E_INVAL if perm is inappropriate (see sys_page_alloc).
//	-E_INVAL if (perm & PTE_W), but srcva is read-only in srcenvid's
//		address space.
//	-E_NO_MEM if there's no memory to allocate any necessary page tables.
    static int
sys_page_map(envid_t srcenvid, void *srcva,
        envid_t dstenvid, void *dstva, int perm)
{
    // Hint: This function is a wrapper around page_lookup() and
    //   page_insert() from kern/pmap.c.
    //   Again, most of the new code you write should be to check the
    //   parameters for correctness.
    //   Use the third argument to page_lookup() to
    //   check the current permissions on the page.

    // LAB 4: Your code here.
    
    if ((uint64_t)srcva >= UTOP || (srcva != ROUNDUP(srcva, PGSIZE) && srcva != ROUNDDOWN(srcva, PGSIZE)) ||
	    (uint64_t)dstva >= UTOP || (dstva != ROUNDUP(dstva, PGSIZE) && dstva != ROUNDDOWN(dstva, PGSIZE)))
                return -E_INVAL;

	if (!(perm & PTE_U) || !(perm & PTE_P))
                return -E_INVAL;

        struct Env *srcenv, *dstenv;
        int srcerr = envid2env(srcenvid, &srcenv, 1);
        int dsterr = envid2env(dstenvid, &dstenv, 1);
        if (srcerr < 0)
                return srcerr;
	else if (dsterr < 0)
		return dsterr;

        else if (srcerr == 0 && dsterr==0) {
       		pte_t *pte;
        	struct Page *pp = page_lookup(srcenv->env_pml4e, srcva, &pte);

        	if (pp == NULL)
                	return -E_INVAL;
	        if (!(*pte & PTE_U))
                	return -E_INVAL;

                if (page_insert(dstenv->env_pml4e, pp, dstva, perm) < 0)
                        return -E_NO_MEM;
		else
			return 0;
	}
    panic("sys_page_map not implemented");
}

// Unmap the page of memory at 'va' in the address space of 'envid'.
// If no page is mapped, the function silently succeeds.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if va >= UTOP, or va is not page-aligned.
    static int
sys_page_unmap(envid_t envid, void *va)
{
    // Hint: This function is a wrapper around page_remove().

    // LAB 4: Your code here.
    
    if ((uint64_t)va >= UTOP || va != ROUNDUP(va, PGSIZE))
                return -E_INVAL;

    struct Env *env;
    int err = envid2env(envid, &env, 1);
    if (err < 0)
	return err;
    else if (err == 0) {
	page_remove(env->env_pml4e, va);
	return 0;
    }
    panic("sys_page_unmap not implemented");
}

// Try to send 'value' to the target env 'envid'.
// If srcva < UTOP, then also send page currently mapped at 'srcva',
// so that receiver gets a duplicate mapping of the same page.
//
// The send fails with a return value of -E_IPC_NOT_RECV if the
// target is not blocked, waiting for an IPC.
//
// The send also can fail for the other reasons listed below.
//
// Otherwise, the send succeeds, and the target's ipc fields are
// updated as follows:
//    env_ipc_recving is set to 0 to block future sends;
//    env_ipc_from is set to the sending envid;
//    env_ipc_value is set to the 'value' parameter;
//    env_ipc_perm is set to 'perm' if a page was transferred, 0 otherwise.
// The target environment is marked runnable again, returning 0
// from the paused sys_ipc_recv system call.  (Hint: does the
// sys_ipc_recv function ever actually return?)
//
// If the sender wants to send a page but the receiver isn't asking for one,
// then no page mapping is transferred, but no error occurs.
// The ipc only happens when no errors occur.
//
// Returns 0 on success, < 0 on error.
// Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist.
//		(No need to check permissions.)
//	-E_IPC_NOT_RECV if envid is not currently blocked in sys_ipc_recv,
//		or another environment managed to send first.
//	-E_INVAL if srcva < UTOP but srcva is not page-aligned.
//	-E_INVAL if srcva < UTOP and perm is inappropriate
//		(see sys_page_alloc).
//	-E_INVAL if srcva < UTOP but srcva is not mapped in the caller's
//		address space.
//	-E_INVAL if (perm & PTE_W), but srcva is read-only in the
//		current environment's address space.
//	-E_NO_MEM if there's not enough memory to map srcva in envid's
//		address space.
    static int
sys_ipc_try_send(envid_t envid, uint32_t value, void *srcva, unsigned perm)
{
    // LAB 4: Your code here.
	struct Env *env;
	pte_t *pte = NULL;
	struct Page *gu_pa = NULL;
	uint64_t ept_ret = 0;
	int i = 0;

	if (envid2env(envid, &env, 0) < 0) {
		cprintf("sys_ipc_try_send failed: Bad env\n");
		return -E_BAD_ENV;
	}
//cprintf("ABHIROOP:%d:\n", __LINE__);
	if ((env->env_status != ENV_NOT_RUNNABLE) || (env->env_ipc_recving != 1))
	    return -E_IPC_NOT_RECV;
	
	if ((uint64_t)srcva < UTOP){
		//Check if the page in alligned.
	   	if ((uint64_t)srcva % PGSIZE != 0) {
			cprintf("sys_ipc_try_send failed: Page is not alligned\n");
	      		return -E_INVAL;
		}
		if (curenv->env_type != ENV_TYPE_GUEST)
		{
		    gu_pa = page_lookup(curenv->env_pml4e,srcva, &pte);
		}
		else
		{
		    ept_ret = e_pml4e_walk(curenv->env_pml4e, srcva, 0);
//cprintf("ABHIROOP:%d:0x%x:srcva:0x%x\n", __LINE__, ept_ret), srcva;
//llubu: ?		    if (ept_ret == E_NO_ENT || ept_ret == E_NO_MEM)
//		    {
//			return -ept_ret;
//		    }
		    pte = (pte_t *) ept_ret;
		    gu_pa = pa2page((physaddr_t)(PTE_ADDR(*pte)));
		}
//cprintf("ABHIROOP:%d:\n", __LINE__);
		if (gu_pa == NULL)
		{
		    cprintf("\n SYS_TRY_IPC_SEND: PAGE NOT FOUND\n");
			return -1;
		}

	  	//Check if srcva is mapped to in caller's address space.

//cprintf("ABHIROOP:%d:\n", __LINE__);
	   	if (!pte || !((*pte) & PTE_P)) {
	      		cprintf("\nsys_ipc_try_send failed: Page is not mapped to srcva\n");
	      		return -E_INVAL;
	   	}
	
	   	//Check for permissions
	   	if (!(perm & PTE_U) || !(perm & PTE_P)) {
	      		cprintf("\nsys_try_ipc_send failed: Invalid permissions\n");
	      		return -E_INVAL;
	   	}
	   	if (perm &(!PTE_USER)) {
       	      		cprintf("sys_try_ipc_send failed: Invalid permissions\n");
       	      		return - E_INVAL;
    	   	}
	   	if (!((*pte) & PTE_W) && (perm & PTE_W)) {
	      		cprintf("\nsys_try_ipc_send failed: Writing on read-only page\n");
	      		return -E_INVAL; 
	   	}

	}

//cprintf("ABHIROOP:%d:\n", __LINE__);
	env->env_ipc_recving = 0;
	env->env_ipc_value = value;
	env->env_ipc_from = curenv->env_id;
	env->env_ipc_perm = 0;
	
	if ((uint64_t)srcva < UTOP) {
	/*	pte_t *pite;
		struct Page *pp;
	   	pp = page_lookup(curenv->env_pml4e, srcva, &pte);
	   
	   	if (!pp) {
	      		cprintf("\nsys_try_ipc_send: Page not found\n");
	      		return -1;
	   	} */

		if (env->env_type != ENV_TYPE_GUEST)
		{
		    if (page_insert(env->env_pml4e, gu_pa,env->env_ipc_dstva, perm) < 0)
		    {
			return -E_NO_MEM;
		    }
		}
		else
		{
		    if (ept_page_insert(env->env_pml4e, gu_pa, env->env_ipc_dstva, perm) < 0)
		    {
			return -E_NO_MEM;
		    }
		}
		env->env_ipc_perm = perm;
	}
	
//cprintf("ABHIROOP:%d:\n", __LINE__);
	env->env_status = ENV_RUNNABLE;
	return 0;

    panic("sys_ipc_try_send not implemented");
}

// Block until a value is ready.  Record that you want to receive
// using the env_ipc_recving and env_ipc_dstva fields of struct Env,
// mark yourself not runnable, and then give up the CPU.
//
// If 'dstva' is < UTOP, then you are willing to receive a page of data.
// 'dstva' is the virtual address at which the sent page should be mapped.
//
// This function only returns on error, but the system call will eventually
// return 0 on success.
// Return < 0 on error.  Errors are:
//	-E_INVAL if dstva < UTOP but dstva is not page-aligned.
    static int
sys_ipc_recv(void *dstva)
{
    // LAB 4: Your code here.
    void *hva;
    if (!curenv)
	return -E_INVAL;
    if ((uint64_t)dstva > UTOP || (ROUNDDOWN(dstva,PGSIZE)!=dstva && ROUNDUP(dstva,PGSIZE)!=dstva)) {
		cprintf("error dstva is not valid dstva=%x\n",dstva);
		return -E_INVAL;
	}

	//Set all required variables.
	curenv->env_tf.tf_regs.reg_rax = 0; //We need this to inform lib/syscall.c that everything went fine.
					    //Note that this function does not return. Thus, set rax so that
					    //if anyone schedules this env, it returns will rax=0 in lib/syscall.

/*	if(curenv->env_type == ENV_TYPE_GUEST)
	{

	    ept_gpa2hva(curenv->env_pml4e,dstva,&hva);
	    curenv->env_ipc_dstva = hva;
	}
	else
	{
	    curenv->env_ipc_dstva = dstva;
	} */
        curenv->env_ipc_dstva = dstva;
        curenv->env_ipc_perm = 0;
        curenv->env_ipc_from = 0;
        curenv->env_ipc_recving = 1; //Receiver is ready to listen
        curenv->env_status = ENV_NOT_RUNNABLE; //Block the execution of current env.
	

	sched_yield(); //Give up the cpu. Don't return, instead env_run some other env.

    panic("sys_ipc_recv not implemented");
    return 0;
}

// Return the current time.
    static int
sys_time_msec(void)
{
    // LAB 6: Your code here.
    return time_msec();
//    panic("sys_time_msec not implemented");
}

// Network related sycalls 
int
sys_net_try_send(char * data, int len)
{

	if ((uintptr_t)data >= UTOP)
	{
	    cprintf("\n NET SEND ERROR UTOP \n");
		return -E_INVAL;
	}

	return e1000_transmit(data, len);
}

int
sys_net_try_receive(char *data, int *len)
{
    int r;
    if ((uintptr_t)data >= UTOP)
    {
	return -E_INVAL;
    }
  //  cprintf("\n sys_net_try_receive \n");
    *len = e1000_receive(data);
    if (*len > 0)
    {
	return 0;
    }
    
    return *len;
}


// Maps a page from the evnironment corresponding to envid into the guest vm 
// environments phys addr space. 
//
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if srcenvid and/or guest doesn't currently exist,
//		or the caller doesn't have permission to change one of them.
//	-E_INVAL if srcva >= UTOP or srcva is not page-aligned,
//		or guest_pa >= guest physical size or guest_pa is not page-aligned.
//	-E_INVAL is srcva is not mapped in srcenvid's address space.
//	-E_INVAL if perm is inappropriate 
//	-E_INVAL if (perm & PTE_W), but srcva is read-only in srcenvid's
//		address space.
//	-E_NO_MEM if there's no memory to allocate any necessary page tables. 
//
// Hint: The TA solution uses ept_map_hva2gpa().  A guest environment uses 
//       env_pml4e to store the root of the extended page tables.
// 

static int
sys_ept_map(envid_t srcenvid, void *srcva,
	    envid_t guest, void* guest_pa, int perm)
{
    /* Your code here */
    int val = 0;
    struct Page *tmp_map = NULL; 
    pte_t *host_pte = NULL;
    pte_t * host_ident = NULL;

    if ((uint64_t)srcva >= UTOP || (srcva != ROUNDUP(srcva, PGSIZE) && srcva != ROUNDDOWN(srcva, PGSIZE)) ||
	     (guest_pa != ROUNDUP(guest_pa, PGSIZE) && guest_pa != ROUNDDOWN(guest_pa, PGSIZE))) {
	cprintf("%d\n", __LINE__);
	return -E_INVAL;
    }

//	if (!(perm & PTE_U) || !(perm & PTE_P)) check if necessary
//                return -E_INVAL;

        struct Env *srcenv, *dstenv;
        int srcerr = envid2env(srcenvid, &srcenv, 1);
        int dsterr = envid2env(guest, &dstenv, 1);
        if (srcerr < 0 || dsterr < 0)
	    return -E_BAD_ENV;
// ERROR CHECK DONE
	if (srcerr == 0 && dsterr == 0)
	{
	    if ((uint64_t)guest_pa + PGSIZE > dstenv->env_vmxinfo.phys_sz)
	    {
		cprintf("GUEST:%x:%d\n", dstenv->env_vmxinfo.phys_sz, __LINE__);
		return -E_INVAL;
	    }
	    tmp_map = page_lookup(srcenv->env_pml4e, (void *)srcva, (pte_t **)&host_pte);		//this gives me page for the srcva

	    if ((perm & __EPTE_WRITE) && (!(*host_pte & PTE_W))) 
	    {
		cprintf("\n Failing in write check permissions \n");
		return -E_INVAL;
	    }
	    host_ident = page2kva(tmp_map);
	

	    val = ept_map_hva2gpa(dstenv->env_pml4e, (void *)host_ident, (void *)guest_pa, perm, 1);  // change the srcva to host_ident
	}
	if (val < 0)
	{
	    return val;
	}
	else
	{
	    tmp_map = page_lookup(srcenv->env_pml4e, (void *) srcva, (pte_t**)NULL);
	    if (tmp_map)
		tmp_map->pp_ref++;
	    else
		cprintf("\n CANT GET PAGE HANDLE IN EPT_MAP \n");
	    return 0;
	}

    panic ("sys_ept_map not implemented");
//    return 0;
}

static envid_t
sys_env_mkguest(uint64_t gphysz, uint64_t gRIP) {
    int r;
    struct Env *e;
	cprintf("\n Making guest environment \n");
    if ((r = env_guest_alloc(&e, curenv->env_id)) < 0)
        return r;
    e->env_status = ENV_NOT_RUNNABLE;
    e->env_vmxinfo.phys_sz = gphysz;
    e->env_tf.tf_rip = gRIP;
    return e->env_id;
}


// Dispatches to the correct kernel function, passing the arguments.
    int64_t
syscall(uint64_t syscallno, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    // Call the function corresponding to the 'syscallno' parameter.
    // Return any appropriate return value.
    // LAB 3: Your code here.

//    panic("syscall not implemented");

    int err;
    switch (syscallno) 
    {
	case SYS_cputs:
	    sys_cputs((char*)a1, a2);
	    break;
	case SYS_cgetc:
	    return sys_cgetc();
	case SYS_getenvid:
	    return sys_getenvid();
	case SYS_env_destroy:
	    return sys_env_destroy(a1);
	case SYS_yield:
	    sys_yield();
	    break;
	case SYS_exofork:
	    return sys_exofork();
	case SYS_env_set_status:
	    return sys_env_set_status(a1, a2);
	case SYS_page_alloc:
	    return sys_page_alloc(a1, (void*)a2, a3);
	case SYS_page_map:
	    return sys_page_map(a1, (void*)a2, a3, (void*)a4, a5);
	case SYS_page_unmap:
	    return sys_page_unmap(a1, (void*)a2);
	case SYS_env_set_pgfault_upcall:
	    return sys_env_set_pgfault_upcall(a1, (void*)a2);
	case SYS_ipc_try_send:
	    return sys_ipc_try_send(a1, a2, (void*)a3, a4);
	case SYS_ipc_recv:
	    return sys_ipc_recv((void*)a1);
	case SYS_env_set_trapframe:
	    return sys_env_set_trapframe(a1, (struct Trapframe*)a2);
	case SYS_time_msec:
	    return sys_time_msec();
//	case SYS_env_transmit_packet:
//	    return sys_env_transmit_packet(a1, (char*)a2, a3);
//	case SYS_env_receive_packet:
//	    return sys_env_receive_packet(a1, (char*)a2, (size_t*)a3);
//	case SYS_get_block_info:
//	    return sys_get_block_info();
//	default:
//	    return -E_INVAL;
//	    break;
	case SYS_net_try_send:
	    return sys_net_try_send((char *)a1, (int) a2);
	case SYS_net_try_receive:
	    return sys_net_try_receive((char *)a1, (int *)a2);

// Virtualization related Syscalls

        case SYS_ept_map:
	    return sys_ept_map(a1, (void*) a2, a3, (void*) a4, a5);

        case SYS_env_mkguest:
            return sys_env_mkguest(a1, a2);

        default:
	    panic("SYS CALL NOT IMPLEMENTED");
            return -E_NO_SYS;
	    break;
    }
    return 0;
}

#ifdef TEST_EPT_MAP
int
_export_sys_ept_map(envid_t srcenvid, void *srcva,
	    envid_t guest, void* guest_pa, int perm)
{
	return sys_ept_map(srcenvid, srcva, guest, guest_pa, perm);
}
#endif

