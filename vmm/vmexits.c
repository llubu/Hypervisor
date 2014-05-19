
#include <vmm/vmx.h>
#include <inc/error.h>
#include <vmm/vmexits.h>
#include <vmm/ept.h>
#include <inc/x86.h>
#include <inc/assert.h>
#include <kern/pmap.h>
#include <kern/console.h>
#include <kern/kclock.h>
#include <kern/multiboot.h>
#include <inc/string.h>
#include <kern/syscall.h>
#include <kern/env.h>
#include <kern/sched.h>
#include <kern/e1000.h>

extern char *multiboot_info;

bool
find_msr_in_region(uint32_t msr_idx, uintptr_t *area, int area_sz, struct vmx_msr_entry **msr_entry) {
    struct vmx_msr_entry *entry = (struct vmx_msr_entry *)area;
    int i;
    for(i=0; i<area_sz; ++i) {
        if(entry->msr_index == msr_idx) {
            *msr_entry = entry;
            return true;
        }
    }
    return false;
}

bool
handle_rdmsr(struct Trapframe *tf, struct VmxGuestInfo *ginfo) {
    uint64_t msr = tf->tf_regs.reg_rcx;
    if(msr == EFER_MSR) {
        // TODO: setup msr_bitmap to ignore EFER_MSR
        uint64_t val;
        struct vmx_msr_entry *entry;
        bool r = find_msr_in_region(msr, ginfo->msr_guest_area, ginfo->msr_count, &entry);
        assert(r);
        val = entry->msr_value;

        tf->tf_regs.reg_rdx = val << 32;
        tf->tf_regs.reg_rax = val & 0xFFFFFFFF;

        tf->tf_rip += vmcs_read32(VMCS_32BIT_VMEXIT_INSTRUCTION_LENGTH);
        return true;
    }

    return false;
}

bool 
handle_wrmsr(struct Trapframe *tf, struct VmxGuestInfo *ginfo) {
    uint64_t msr = tf->tf_regs.reg_rcx;
    if(msr == EFER_MSR) {

        uint64_t cur_val, new_val;
        struct vmx_msr_entry *entry;
        bool r = 
            find_msr_in_region(msr, ginfo->msr_guest_area, ginfo->msr_count, &entry);
        assert(r);
        cur_val = entry->msr_value;

        new_val = (tf->tf_regs.reg_rdx << 32)|tf->tf_regs.reg_rax;
        if(BIT(cur_val, EFER_LME) == 0 && BIT(new_val, EFER_LME) == 1) {
            // Long mode enable.
            uint32_t entry_ctls = vmcs_read32( VMCS_32BIT_CONTROL_VMENTRY_CONTROLS );
            entry_ctls |= VMCS_VMENTRY_x64_GUEST;
            vmcs_write32( VMCS_32BIT_CONTROL_VMENTRY_CONTROLS, 
                    entry_ctls );

        }

        entry->msr_value = new_val;
        tf->tf_rip += vmcs_read32(VMCS_32BIT_VMEXIT_INSTRUCTION_LENGTH);
        return true;
    }

    return false;
}

bool
handle_eptviolation(uint64_t *eptrt, struct VmxGuestInfo *ginfo) {
    uint64_t gpa = vmcs_read64(VMCS_64BIT_GUEST_PHYSICAL_ADDR);
    int r;
//    cprintf("EPT_VIO:0x%x::\n", gpa);
    if(gpa < 0xA0000 || (gpa >= 0x100000 && gpa < ginfo->phys_sz)) {
        // Allocate a new page to the guest.
        struct Page *p = page_alloc(0);
        if(!p)
            return false;
        p->pp_ref += 1;
        r = ept_map_hva2gpa(eptrt, 
                page2kva(p), (void *)ROUNDDOWN(gpa, PGSIZE), __EPTE_FULL, 0);
        assert(r >= 0);
        /* cprintf("EPT violation for gpa:%x mapped KVA:%x\n", gpa, page2kva(p)); */
        return true;
    } else if (gpa >= CGA_BUF && gpa < CGA_BUF + PGSIZE) {
        // FIXME: This give direct access to VGA MMIO region.
        r = ept_map_hva2gpa(eptrt, 
                (void *)(KERNBASE + CGA_BUF), (void *)CGA_BUF, __EPTE_FULL, 0);
        assert(r >= 0);
        return true;
    }

    else if (gpa >= 0xF0000 && gpa <= 0xF0000  + 0x10000) {
	r = ept_map_hva2gpa(eptrt, (void *)(KERNBASE + gpa), (void *)gpa, __EPTE_FULL, 0);
	assert(r >= 0);
	return true;
    } else if (gpa >= 0xfee00000 ) {
	r = ept_map_hva2gpa(eptrt, (void *)(KERNBASE + gpa), (void *)(gpa), __EPTE_FULL, 0);
	assert(r >= 0);
	return true;
    }   
    return false;
}

bool
handle_ioinstr(struct Trapframe *tf, struct VmxGuestInfo *ginfo) {
    static int port_iortc;

    uint64_t qualification = vmcs_read64(VMCS_VMEXIT_QUALIFICATION);
    int port_number = (qualification >> 16) & 0xFFFF;
    bool is_in = BIT(qualification, 3);
    bool handled = false;

    // handle reading physical memory from the CMOS.
    if(port_number == IO_RTC) {
        if(!is_in) {
            port_iortc = tf->tf_regs.reg_rax;
            handled = true;
        }
    } else if (port_number == IO_RTC + 1) {
        if(is_in) {
            if(port_iortc == NVRAM_BASELO) {
                tf->tf_regs.reg_rax = 640 & 0xFF;
                handled = true;
            } else if (port_iortc == NVRAM_BASEHI) {
                tf->tf_regs.reg_rax = (640 >> 8) & 0xFF;
                handled = true;
            } else if (port_iortc == NVRAM_EXTLO) {
                tf->tf_regs.reg_rax = ((ginfo->phys_sz / 1024) - 1024) & 0xFF;
                handled = true;
            } else if (port_iortc == NVRAM_EXTHI) {
                tf->tf_regs.reg_rax = (((ginfo->phys_sz / 1024) - 1024) >> 8) & 0xFF;
                handled = true;
            }
        }

    }

    if(handled) {
        tf->tf_rip += vmcs_read32(VMCS_32BIT_VMEXIT_INSTRUCTION_LENGTH);
        return true;
    } else {
        cprintf("%x %x\n", qualification, port_iortc);
        return false;    
    }
}

// Emulate a cpuid instruction.
// It is sufficient to issue the cpuid instruction here and collect the return value.
// You can store the output of the instruction in Trapframe tf,
//  but you should hide the presence of vmx from the guest if processor features are requested.
// 
// Return true if the exit is handled properly, false if the VM should be terminated.
//
// Finally, you need to increment the program counter in the trap frame.
// 
// Hint: The TA's solution does not hard-code the length of the cpuid instruction.
bool
handle_cpuid(struct Trapframe *tf, struct VmxGuestInfo *ginfo)
{
    /* Your code here */
//    cprintf("Handle cpuid not implemented\n"); 
    uint32_t mask = 32;
    uint32_t in_rax = (uint32_t) tf->tf_regs.reg_rax;
    uint32_t eax, ebx, ecx, edx;
    cpuid(in_rax, &eax, &ebx, &ecx, &edx );
    if (in_rax == 1)
    {
	ecx = ecx & ~mask;
    }
    tf->tf_regs.reg_rax = (uint64_t)eax;
    tf->tf_regs.reg_rbx = (uint64_t)ebx;
    tf->tf_regs.reg_rcx = (uint64_t)ecx;
    tf->tf_regs.reg_rdx = (uint64_t)edx;

    tf->tf_rip += vmcs_read32(VMCS_32BIT_VMEXIT_INSTRUCTION_LENGTH);
	    
   return true;

}

// Handle vmcall traps from the guest.
// We currently support 3 traps: read the virtual e820 map, 
//   and use host-level IPC (send andrecv).
//
// Return true if the exit is handled properly, false if the VM should be terminated.
//
// Finally, you need to increment the program counter in the trap frame.
// 
// Hint: The TA's solution does not hard-code the length of the cpuid instruction.//

bool
handle_vmcall(struct Trapframe *tf, struct VmxGuestInfo *gInfo, uint64_t *eptrt)
{
    bool handled = false;
    multiboot_info_t mbinfo;
    memory_map_t tmp_arr[3];		// Tmp arr to create multiboot map
    int perm, r, i;
    void *gpa_pg, *hva_pg;
    envid_t to_env;
    uint32_t val;
    int ret = -1;
    epte_t *epte_out = NULL;
    struct Page *tmp_page = NULL;
    physaddr_t page_addr = 0x0;
    pte_t *pte_page = NULL;
    pte_t *host_va = NULL;
    uint64_t gpa_net;
    uintptr_t *hva_net;
    int *len_pt;	// to pass packet length to the guest
    int rcv_len = 0;
    int rv_ret = 0;
    int len = 0;
    

    // phys address of the multiboot map in the guest.
    uint64_t multiboot_map_addr = 0x6000;

    switch(tf->tf_regs.reg_rax) {
        case VMX_VMCALL_MBMAP:
            // Craft a multiboot (e820) memory map for the guest.
	    //
            // Create three  memory mapping segments: 640k of low mem, the I/O hole (unusable), and 
	    //   high memory (phys_size - 1024k).
	    //
	    // Once the map is ready, find the kernel virtual address of the guest page (if present),
	    //   or allocate one and map it at the multiboot_map_addr (0x6000).
	    // Copy the mbinfo and memory_map_t (segment descriptions) into the guest page, and return
	    //   a pointer to this region in rbx (as a guest physical address).
	    /* Your code here */
	
            tmp_arr[0].size = 20;
	    tmp_arr[0].base_addr_low = 0x0;
	    tmp_arr[0].base_addr_high = 0x0;
	    tmp_arr[0].length_low = IOPHYSMEM;
	    tmp_arr[0].length_high = 0x0;
	    tmp_arr[0].type = MB_TYPE_USABLE;
  
	    tmp_arr[1].size = 20;
	    tmp_arr[1].base_addr_low = IOPHYSMEM;
	    tmp_arr[1].base_addr_high = 0x0;
	    tmp_arr[1].length_low = 0x60000;
	    tmp_arr[1].length_high = 0x0;
	    tmp_arr[1].type = MB_TYPE_RESERVED;
            
	    tmp_arr[2].size = 20;
	    tmp_arr[2].base_addr_low = EXTPHYSMEM;
	    tmp_arr[2].base_addr_high = 0x0;
	    tmp_arr[2].length_low = (uint32_t)(gInfo->phys_sz - EXTPHYSMEM) & (0xffffffff);
	    tmp_arr[2].length_high =(uint32_t)((gInfo->phys_sz - EXTPHYSMEM) >> 32) & (0xffffffff);
	    tmp_arr[2].type = MB_TYPE_USABLE;

	    mbinfo.flags = MB_FLAG_MMAP;
	    mbinfo.mmap_length = sizeof(tmp_arr);
	    mbinfo.mmap_addr =  multiboot_map_addr + sizeof(multiboot_info_t);

  		
	    if (( tmp_page = page_alloc(ALLOC_ZERO)) == NULL)
	    {
		return false;
	    }
	    tmp_page->pp_ref++;
	    host_va = page2kva(tmp_page);

//	    if ((ret = ept_lookup_gpa((epte_t *)eptrt, (void *)multiboot_map_addr, 0, (epte_t **) &epte_out)) == -ENO_ENT)
//		{ // mapping is not present in guest for 0x6000
		    memcpy((void *)host_va,(void *)& mbinfo, (size_t)sizeof(multiboot_info_t)); 
		    memcpy(((void *)host_va + sizeof(multiboot_info_t)), (void *)tmp_arr,sizeof(tmp_arr));
//	        }
	ept_map_hva2gpa((epte_t*) eptrt, (void *) host_va, (void *)multiboot_map_addr, __EPTE_FULL, 1);	
	    tf->tf_regs.reg_rbx = (uint64_t) multiboot_map_addr;

//    cprintf("e820 map hypercall not implemented\n");	    
	    handled = true;
	    break;
        case VMX_VMCALL_IPCSEND:
	    // Issue the sys_ipc_send call to the host.
	    // 
	    // If the requested environment is the HOST FS, this call should
	    //  do this translation.
	    /* Your code here */
//	    cprintf("ABHIROOP:%d:\n",__LINE__);
	    to_env = tf->tf_regs.reg_rdx;
	    if ( to_env == 1 && curenv->env_type == ENV_TYPE_GUEST)
	    {
//	    cprintf("ABHIROOP:%d:\n",__LINE__);
		for (i = 0; i < NENV; i++)
		{
		    if (envs[i].env_type == ENV_TYPE_FS)
		    {
			to_env = (uint64_t)( envs[i].env_id);
			break;
		    }
		}
	    }
			
//	    cprintf("ABHIROOP:%d:%d\n",__LINE__, to_env);
            ret = syscall(SYS_ipc_try_send,(uint64_t) to_env, (uint64_t)tf->tf_regs.reg_rcx, (uint64_t)tf->tf_regs.reg_rbx, (uint64_t)tf->tf_regs.reg_rdi, (uint64_t)0);

//	    cprintf("ABHIROOP:%d:\n",__LINE__);
	    tf->tf_regs.reg_rax = (uint64_t) ret;
//	    cprintf("IPC send hypercall not implemented\n");	    
	    handled = true;
            break;

        case VMX_VMCALL_IPCRECV:
	    // Issue the sys_ipc_recv call for the guest.
	    // NB: because recv can call schedule, clobbering the VMCS, 
	    // you should go ahead and increment rip before this call.
	    /* Your code here */

//	    cprintf("ABHIROOP:%d:\n",__LINE__);
    	    tf->tf_rip += vmcs_read32(VMCS_32BIT_VMEXIT_INSTRUCTION_LENGTH);  //cause it never returns
//	    cprintf("ABHIROOP:%d:\n",__LINE__);
	    ret = syscall(SYS_ipc_recv, (uint64_t)tf->tf_regs.reg_rdx, (uint64_t)0, (uint64_t)0, (uint64_t)0,(uint64_t)0);
// cprintf("IPC recv hypercall not implemented\n");	    
	    tf->tf_regs.reg_rax = (uint64_t)ret;
            handled = true;
            break;

	case VMX_VMCALL_NETSEND:
	    // handles vmcalls for NW send requests from the guest
	    gpa_net =  tf->tf_regs.reg_rdx;
	    len = tf->tf_regs.reg_rcx;
		
	    ept_gpa2hva(eptrt, (void *) gpa_net, (void *) &hva_net);
//	    cprintf("GPA: HVA IS: 0x%x:0x%x\n", gpa_net, hva_net);
//	    cprintf("LEN IS :%d:\n", tf->tf_regs.reg_rcx);
	    ret = -1;
//	    ret = syscall(SYS_net_try_send, (uint64_t) hva_net, (uint64_t)tf->tf_regs.reg_rcx, (uint64_t)0, (uint64_t)0,(uint64_t)0) ; 
	    ret = e1000_transmit((char *) hva_net, len);
	    tf->tf_regs.reg_rax = (uint64_t) ret;
//	    cprintf("RET IS :%d:\n", ret);
	    handled = true;
	    break;
	
	case VMX_VMCALL_NETRECV:
	    // handles vmcall for NW receive requests from the guest

	    gpa_net = tf->tf_regs.reg_rdx;
	    len_pt = (int *) tf->tf_regs.reg_rcx;	// pointer to len variable in input.c in guest

	    ept_gpa2hva(eptrt, (void *) gpa_net, (void *) &hva_net);
//	    cprintf("RCV:GPA: HVA IS: 0x%x:0x%x\n", gpa_net, hva_net);
//	    cprintf("RCV:LEN IS :%d:\n", tf->tf_regs.reg_rcx);
	    // copying pkt 

	   ret = (int) guest_rcv((char *) hva_net, &rcv_len, &rv_ret); //ret is the len of the pkt returned 
//	   cprintf("RET-ABHI- IN RCV VMEXIT IS:%d:\n", rv_ret);
	   if (rv_ret == 0)
	   {
	       tf->tf_regs.reg_rax = (uint64_t) rcv_len;

	       char *buf=(char *)hva_net;
	       int i=0;
//	       cprintf("\n DATA in VMEXIST  of len=[%d]=[",rcv_len);
//	       for(i=0;i<rcv_len;i++)
//		   cprintf("%u ",buf[i]);
//	       cprintf("]\n");
	   }
	   else
	   {
	       tf->tf_regs.reg_rax = (uint64_t) 0;
	   }

	   handled = true;
	   break;
    }


    if(handled) {
	    /* Advance the program counter by the length of the vmcall instruction. 
	     * 
	     * Hint: The TA solution does not hard-code the length of the vmcall instruction.
	     */
	    /* Your code here */
    		tf->tf_rip += vmcs_read32(VMCS_32BIT_VMEXIT_INSTRUCTION_LENGTH);
    }
    return handled;
}

