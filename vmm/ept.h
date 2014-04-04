
#ifndef JOS_VMX_EPT_H
#define JOS_VMX_EPT_H

#include <inc/mmu.h>
#include <vmm/vmx.h>
#include <inc/ept.h>

typedef uint64_t epte_t;

int ept_map_hva2gpa( epte_t* eptrt, void* hva, void* gpa, int perm, int overwrite );
int ept_alloc_static(epte_t *eptrt, struct VmxGuestInfo *ginfo);
void free_guest_mem(epte_t* eptrt);
void ept_gpa2hva(epte_t* eptrt, void *gpa, void **hva);
int ept_page_insert(epte_t* eptrt, struct Page* pp, void* gpa, int perm);
uint64_t e_pml4e_walk(epte_t *eptrt, void *gpa, int create);
uint64_t e_pdpe_walk(pdpe_t *pdpe, void *gpa, int create);
uint64_t e_pgdir_walk(pde_t *pgdir, void *gpa, int create);
uint64_t e_pml4e_walk(epte_t *eptrt, void *gpa, int create);
//int _export_sys_ept_map(envid_t srcenvid, void *srcva,envid_t guest, void* guest_pa, int perm);
//int test_ept_map(void);	//test eptfunction
#define EPT_LEVELS 4

#define VMX_EPT_FAULT_READ	0x01
#define VMX_EPT_FAULT_WRITE	0x02
#define VMX_EPT_FAULT_INS	0x04


#define EPTE_ADDR	(~(PGSIZE - 1))
#define EPTE_FLAGS	(PGSIZE - 1)

#define ADDR_TO_IDX(pa, n) \
    ((((uint64_t) (pa)) >> (12 + 9 * (n))) & ((1 << 9) - 1))

#endif
