
#include <vmm/ept.h>

#include <inc/error.h>
#include <inc/memlayout.h>
#include <kern/pmap.h>
#include <inc/string.h>
#include <kern/env.h>

// Return the physical address of an ept entry
static inline uintptr_t epte_addr(epte_t epte)
{
	return (epte & EPTE_ADDR);
}

// Return the host kernel virtual address of an ept entry
static inline uintptr_t epte_page_vaddr(epte_t epte)
{
	return (uintptr_t) KADDR(epte_addr(epte));
}

// Return the flags from an ept entry
static inline epte_t epte_flags(epte_t epte)
{
	return (epte & EPTE_FLAGS);
}

// Return true if an ept entry's mapping is present
static inline int epte_present(epte_t epte)
{
	return (epte & __EPTE_FULL) > 0;
}

// Find the final ept entry for a given guest physical address,
// creating any missing intermediate extended page tables if create is non-zero.
//
// If epte_out is non-NULL, store the found epte_t* at this address.
//
// Return 0 on success.  
// 
// Error values:
//    -E_INVAL if eptrt is NULL
//    -E_NO_ENT if create == 0 and the intermediate page table entries are missing.
//    -E_NO_MEM if allocation of intermediate page table entries fails
//
// Hint: Set the permissions of intermediate ept entries to __EPTE_FULL.
//       The hardware ANDs the permissions at each level, so removing a permission
//       bit at the last level entry is sufficient (and the bookkeeping is much simpler).
static int ept_lookup_gpa(epte_t* eptrt, void *gpa, 
			  int create, epte_t **epte_out) 
{
    uint64_t val = 0;

    /* Your code here */
//    cprintf("\n IN EPT_LOOKUP_GPA\n");
    if ( NULL == eptrt)
    {
	return -E_INVAL;
    }
    val = e_pml4e_walk(eptrt, gpa, create);
    if (val == E_NO_ENT || val == E_NO_MEM)
    {
	return -val;
    }
    else
    {
	if (epte_out != NULL)
	{
	    *epte_out = (epte_t *) val;
	    return 0;
	}
	else  //should not return a errorn in this case
	    return -E_INVAL;	// return this when epte_out is NULL
    }


//    panic("ept_lookup_gpa not implemented\n");
//    return 0;

}

// Helper function for EPTE WALK same as PT* walks
uint64_t e_pml4e_walk(epte_t *eptrt, void *gpa, int create)
{
    uintptr_t index_in_epml4e = PML4(gpa);
    pml4e_t *offset_ptr_in_epml4e = eptrt + index_in_epml4e;
    pdpe_t *epdpe_base = (pdpe_t *) (PTE_ADDR(*offset_ptr_in_epml4e));

    if (NULL == epdpe_base)
    {
	if (0 == create)
	{
	    return E_NO_ENT;
	}
	else
	{
	    struct Page *new_epdpe = page_alloc(ALLOC_ZERO);

	    if (NULL == new_epdpe )
	    {
//		cprintf("LINE %d\n", __LINE__);
		return E_NO_MEM;
	    }

	    new_epdpe->pp_ref++;
	    epdpe_base = (pdpe_t *)(page2pa(new_epdpe));
	    pte_t *epte = (pte_t *) e_pdpe_walk((pdpe_t *)page2kva(new_epdpe), gpa, create);

	    if (NULL == epte || epte == (pte_t *)E_NO_MEM || epte == (pte_t *)E_NO_ENT)
	    {
//		cprintf("LINE %d %p\n", __LINE__, epte);
		page_decref(new_epdpe);
		return E_NO_MEM;
	    }
	    else
	    {
		*offset_ptr_in_epml4e = ((uint64_t)epdpe_base) | __EPTE_FULL ;
		return (uint64_t)epte;
	    }
	}
    }
    else
    {
	return (uint64_t) e_pdpe_walk(KADDR((uint64_t)epdpe_base), gpa, create);
    }
//    return 0;
}

// Helper to walk ePdpe level PT same as pdpe_walk

uint64_t e_pdpe_walk(pdpe_t *pdpe, void *gpa, int create)
{
    uintptr_t index_in_epdp = PDPE(gpa);
    pdpe_t *offset_ptr_in_epdpe = pdpe + index_in_epdp;
    pde_t *epgdir_base = (pde_t *) PTE_ADDR(*offset_ptr_in_epdpe);

    if ( NULL == epgdir_base )
    {
	if ( 0 == create )
	{
	    return E_NO_ENT;
	}
	else
	{
	    struct Page *new_epde = page_alloc(ALLOC_ZERO);

	    if (!new_epde)
	    {
//		cprintf("LINE %d\n", __LINE__);
		return E_NO_MEM;
	    }

	    new_epde->pp_ref++;
	    epgdir_base = (pde_t *)page2pa(new_epde);
	    pte_t *epte = (pte_t *) e_pgdir_walk(page2kva(new_epde), gpa, create);

	    if (NULL == epte || epte == (pte_t *)E_NO_MEM || epte == (pte_t *)E_NO_ENT)
	    {
//		cprintf("LINE %d %p\n", __LINE__, epte);
		page_decref(new_epde);
		return E_NO_MEM; 
	    }
	    else
	    {
		*offset_ptr_in_epdpe = ((uint64_t)epgdir_base) | __EPTE_FULL;
		return (uint64_t) epte;
	    }
	}
    }
    else
    {
	return (uint64_t) e_pgdir_walk(KADDR((uint64_t)epgdir_base), gpa, create);
    }
//    return 0;
}

// Helper for pgdir walk for EPT, same as pgdir_walk


uint64_t e_pgdir_walk(pde_t *pgdir, void *gpa, int create)
{
    uintptr_t index_in_epgdir = PDX(gpa);
    pde_t *offset_ptr_in_epgdir = pgdir + index_in_epgdir;
    pte_t *epage_table_base = (pte_t *)(PTE_ADDR(*offset_ptr_in_epgdir));

    if (NULL == epage_table_base)
    {
	if (!create)
	{
//	    cprintf("LINE %d\n", __LINE__);
	    return E_NO_ENT;
	}
	else
	{
	    struct Page *new_PT = page_alloc(ALLOC_ZERO);

	    if (NULL == new_PT)
	    {
//		cprintf("LINE %d\n", __LINE__);
		return E_NO_MEM;
	    }
	    new_PT->pp_ref++;
	    epage_table_base = (pte_t *)page2pa(new_PT);
	    *offset_ptr_in_epgdir = ((uint64_t)epage_table_base) | __EPTE_FULL ;

	    uintptr_t index_in_epage_table = PTX(gpa);
	    pte_t *offset_ptr_in_epage_table = epage_table_base + index_in_epage_table;
//		cprintf("LINE %d\n", __LINE__);
	    return (uint64_t)KADDR((uint64_t) offset_ptr_in_epage_table);
	}
    }
    else
    {
	uintptr_t index_in_epage_table = PTX(gpa);
	pte_t *offset_ptr_in_epage_table = epage_table_base + index_in_epage_table;
//		cprintf("LINE %d\n", __LINE__);
	return (uint64_t)KADDR((uint64_t)offset_ptr_in_epage_table);
    }
}
		

void ept_gpa2hva(epte_t* eptrt, void *gpa, void **hva) {
    epte_t* pte;
    int ret = ept_lookup_gpa(eptrt, gpa, 0, &pte);
    if(ret < 0) {
        *hva = NULL;
    } else {
        if(!epte_present(*pte)) {
           *hva = NULL;
        } else {
           *hva = KADDR(epte_addr(*pte));
        }
    }
}

static void free_ept_level(epte_t* eptrt, int level) {
    epte_t* dir = eptrt;
    int i;

    for(i=0; i<NPTENTRIES; ++i) {
        if(level != 0) {
            if(epte_present(dir[i])) {
                physaddr_t pa = epte_addr(dir[i]);
                free_ept_level((epte_t*) KADDR(pa), level-1);
                // free the table.
                page_decref(pa2page(pa));
            }
        } else {
            // Last level, free the guest physical page.
            if(epte_present(dir[i])) {
                physaddr_t pa = epte_addr(dir[i]);
                page_decref(pa2page(pa));
            }
        }
    }
    return;
}

// Free the EPT table entries and the EPT tables.
// NOTE: Does not deallocate EPT PML4 page.
void free_guest_mem(epte_t* eptrt) {
    free_ept_level(eptrt, EPT_LEVELS - 1);
}

// Add Page pp to a guest's EPT at guest physical address gpa
//  with permission perm.  eptrt is the EPT root.
// 
// Return 0 on success, <0 on failure.
//
int ept_page_insert(epte_t* eptrt, struct Page* pp, void* gpa, int perm) {

    /* Your code here */
    int val = 0;
    pte_t *pte = NULL;

    val = ept_lookup_gpa(eptrt, (void*) gpa, 1, &pte);

    if (pte == NULL )
	return -E_NO_MEM;
    if (val < 0)
	return val;
    pp->pp_ref++;
    if(*pte & PTE_P)
          page_remove(eptrt, gpa);
    *pte = ((uint64_t)page2pa(pp)) | perm | __EPTE_IPAT;
    return 0;

}

// Map host virtual address hva to guest physical address gpa,
// with permissions perm.  eptrt is a pointer to the extended
// page table root.
//
// Return 0 on success.
// 
// If the mapping already exists and overwrite is set to 0,
//  return -E_INVAL.
// 
// Hint: use ept_lookup_gpa to create the intermediate 
//       ept levels, and return the final epte_t pointer.
int ept_map_hva2gpa(epte_t* eptrt, void* hva, void* gpa, int perm, 
        int overwrite) {

    /* Your code here */
//    cprintf("\n IN HVA2GPA\n");
    pte_t *pte_host = NULL, *pte_guest = NULL;
    struct Page *pg_pt = NULL;
    int val = 0;
    physaddr_t host_ad = 0x0;

    if (!(perm & __EPTE_FULL))
    {
//    cprintf("LINE %d\n", __LINE__);
	return -E_INVAL;
    }

    host_ad = PADDR(hva);

    val = ept_lookup_gpa(eptrt, gpa, 1, (epte_t**) &(pte_guest));
//    cprintf("LINE %d\n", __LINE__);

    if (val < 0)
    {
//	cprintf("LINE %d\n", __LINE__);
	return val;
    }
    else if (0 == val) 
    {
//	cprintf("LINE %d\n", __LINE__);
	if ((*pte_guest) && overwrite == 0 )
	{
//	    cprintf("LINE %d\n", __LINE__);
	    return -E_INVAL;
	}
	else if (*pte_guest && overwrite == 1 )
	{
	    *pte_guest = (uint64_t )host_ad | perm | __EPTE_IPAT;// | __EPTE_TYPE(EPTE_TYPE_WB);
	    return 0;
	}
	if (!(*pte_guest))
	{
	    *pte_guest = (uint64_t )host_ad | perm | __EPTE_IPAT;// | __EPTE_TYPE(EPTE_TYPE_WB);
//	    *pte_guest = (uint64_t)host_ad | perm;
	    return 0;
	}

    }

 //   panic("ept_map_hva2gpa not implemented\n");

    return val;
}

int ept_alloc_static(epte_t *eptrt, struct VmxGuestInfo *ginfo) {
    physaddr_t i;
    
    for(i=0x0; i < 0xA0000; i+=PGSIZE) {
        struct Page *p = page_alloc(0);
        p->pp_ref += 1;
        int r = ept_map_hva2gpa(eptrt, page2kva(p), (void *)i, __EPTE_FULL, 0);
    }

    for(i=0x100000; i < ginfo->phys_sz; i+=PGSIZE) {
        struct Page *p = page_alloc(0);
        p->pp_ref += 1;
        int r = ept_map_hva2gpa(eptrt, page2kva(p), (void *)i, __EPTE_FULL, 0);
    }
    return 0;
}

#ifdef TEST_EPT_MAP
#include <kern/env.h>
#include <kern/syscall.h>
int _export_sys_ept_map(envid_t srcenvid, void *srcva,
	    envid_t guest, void* guest_pa, int perm);

int test_ept_map(void)
{
	struct Env *srcenv, *dstenv;
	struct Page *pp;
	epte_t *epte;
	int r;

	/* Initialize source env */
	if ((r = env_alloc(&srcenv, 0)) < 0)
		panic("Failed to allocate env (%d)\n", r);
	if (!(pp = page_alloc(ALLOC_ZERO)))
		panic("Failed to allocate page (%d)\n", r);
	if ((r = page_insert(srcenv->env_pml4e, pp, UTEMP, 0)) < 0)
		panic("Failed to insert page (%d)\n", r);
	curenv = srcenv;

	/* Check if sys_ept_map correctly verify the target env */
	if ((r = env_alloc(&dstenv, srcenv->env_id)) < 0)
		panic("Failed to allocate env (%d)\n", r);
	if ((r = _export_sys_ept_map(srcenv->env_id, UTEMP, dstenv->env_id, UTEMP, __EPTE_READ)) < 0)
		cprintf("EPT map to non-guest env failed as expected (%d).\n", r);
	else
		panic("sys_ept_map success on non-guest env.\n");

	/*env_destroy(dstenv);*/

	if ((r = env_guest_alloc(&dstenv, srcenv->env_id)) < 0)
		panic("Failed to allocate guest env (%d)\n", r);
	dstenv->env_vmxinfo.phys_sz = (uint64_t)UTEMP + PGSIZE;

	/* Check if sys_ept_map can verify srcva correctly */
	if ((r = _export_sys_ept_map(srcenv->env_id, (void *)UTOP, dstenv->env_id, UTEMP, __EPTE_READ)) < 0)
		cprintf("EPT map from above UTOP area failed as expected (%d).\n", r);
	else
		panic("sys_ept_map from above UTOP area success\n");
	if ((r = _export_sys_ept_map(srcenv->env_id, UTEMP+1, dstenv->env_id, UTEMP, __EPTE_READ)) < 0)
		cprintf("EPT map from unaligned srcva failed as expected (%d).\n", r);
	else
		panic("sys_ept_map from unaligned srcva success\n");

	/* Check if sys_ept_map can verify guest_pa correctly */
	if ((r = _export_sys_ept_map(srcenv->env_id, UTEMP, dstenv->env_id, UTEMP + PGSIZE, __EPTE_READ)) < 0)
		cprintf("EPT map to out-of-boundary area failed as expected (%d).\n", r);
	else
		panic("sys_ept_map success on out-of-boundary area\n");
	if ((r = _export_sys_ept_map(srcenv->env_id, UTEMP, dstenv->env_id, UTEMP-1, __EPTE_READ)) < 0)
		cprintf("EPT map to unaligned guest_pa failed as expected (%d).\n", r);
	else
		panic("sys_ept_map success on unaligned guest_pa\n");

	/* Check if the sys_ept_map can verify the permission correctly */
	if ((r = _export_sys_ept_map(srcenv->env_id, UTEMP, dstenv->env_id, UTEMP, 0)) < 0)
		cprintf("EPT map with empty perm parameter failed as expected (%d).\n", r);
	else
		panic("sys_ept_map success on empty perm\n");
	if ((r = _export_sys_ept_map(srcenv->env_id, UTEMP, dstenv->env_id, UTEMP, __EPTE_WRITE)) < 0)
		cprintf("EPT map with write perm parameter failed as expected (%d).\n", r);
	else
		panic("sys_ept_map success on write perm\n");

	/* Check if the sys_ept_map can succeed on correct setup */
	if ((r = _export_sys_ept_map(srcenv->env_id, UTEMP, dstenv->env_id, UTEMP, __EPTE_READ)) < 0)
		panic("Failed to do sys_ept_map (%d)\n", r);
	else
		cprintf("sys_ept_map finished normally.\n");

	/* Check if the mapping is valid */
	if ((r = ept_lookup_gpa(dstenv->env_pml4e, UTEMP, 0, &epte)) < 0)
		panic("Failed on ept_lookup_gpa (%d)\n", r);
	if (page2pa(pp) != (epte_addr(*epte)))
		panic("EPT mapping address mismatching (%x vs %x).\n",
				page2pa(pp), epte_addr(*epte));
	else
		cprintf("EPT mapping address looks good: %x vs %x.\n",
				page2pa(pp), epte_addr(*epte));

	/* stop running after test, as this is just a test run. */
	panic("Cheers! sys_ept_map seems to work correctly.\n");

	return 0;
}
#endif

