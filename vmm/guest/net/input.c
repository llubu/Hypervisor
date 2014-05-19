#include "ns.h"
#include <inc/vmx.h>

extern union Nsipc nsipcbuf;


// vmcall for the guest receive funtionality
int net_try_receive(char *data, int *len)
{
    int ret = 0, vmret = -1,i=0;
    uintptr_t tmp_adr = (uintptr_t) data;
    int num = VMX_VMCALL_NETRECV;
    uint64_t a1, a2;
    char * tm_data = (char *)data;

    if (NULL == data)
    {
	return -E_INVAL;
    }

    if ((uintptr_t) data >= UTOP )
    {
	return -E_INVAL;
    }

    if((vpml4e[VPML4E(tmp_adr)] & PTE_P) && (vpde[VPDPE(tmp_adr)] & PTE_P) && (vpd[VPD(tmp_adr)] & PTE_P)  &&  (vpt[VPN(tmp_adr)] & PTE_P))
    {
           data = (void *) PTE_ADDR( vpt[VPN(tmp_adr)] );
    }
    else
    {
	cprintf("\n Receive Packet BUF  not mapped SOMETHING IS REALLY WRONG \n");
	if((ret = sys_page_alloc(0, (void *) tmp_adr , PTE_P|PTE_W|PTE_U|PTE_SHARE)) < 0)
	    return ret;
        data = (void *) PTE_ADDR( vpt[VPN(tmp_adr)] );
    }

    a1 = (uint64_t) data;
    a2 = (uint64_t) len;

    asm volatile("vmcall\n"
		: "=a" (ret)
		: "a" (num),
		  "d" (a1),
		  "c" (a2)
		: "cc", "memory");

//    vmret = (int) ret;
//    *len = (int) ret;
    if (0 == ret)
    {
	vmret = -1;
	*len = 0;
    }
    else
    {
	vmret = ret;
	*len = ret;
    }
//    cprintf("RET IN GUEST INPUT:%d:\n", vmret);
    if (vmret > 0) 
    {
//    cprintf("\n DATA in GUEST_INPUT of len=[%d]=[",*len);
//    	for(i=0;i<*len;i++)
//	    cprintf("%u ",tm_data[i]);
//	cprintf("]\n");
    }
     return vmret;

}

void
input(envid_t ns_envid)
{
	binaryname = "ns_input";

	// LAB 6: Your code here:
	// 	- read a packet from the device driver
	//	- send it to the network server
	// Hint: When you IPC a page to the network server, it will be
	// reading from it for a while, so don't immediately receive
	// another packet in to the same physical page.
//	char buf[2048];
	char *buf = NULL;
	buf = (char *) malloc(sizeof(char) * 2048);
	memset(buf, 0, 2048);
		
	int len = 0, r, i;

	while (1) {
		// 	- read a packet from the device driver
		while ((r = net_try_receive(buf, &len)) < 0)
			sys_yield();
//cprintf("DABRAL:%d:%s\n", __LINE__, __FILE__);
		// Whenever a new page is allocated, old will be deallocated by page_insert automatically.
		while ((r = sys_page_alloc(0, &nsipcbuf, PTE_U | PTE_P | PTE_W)) < 0);
//cprintf("\n LEN AFTER UPDATE is :%d\n", len);
//cprintf("DABRAL:%d:%s\n", __LINE__, __FILE__);
		nsipcbuf.pkt.jp_len = len;
		memmove(nsipcbuf.pkt.jp_data, buf, len);

//cprintf("DABRAL:%d:%s\n", __LINE__, __FILE__);
		while ((r = sys_ipc_try_send(ns_envid, NSREQ_INPUT, &nsipcbuf, PTE_P | PTE_W | PTE_U)) < 0);
	}  
}
