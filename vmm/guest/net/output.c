#include "ns.h"
#include <inc/vmx.h>

extern union Nsipc nsipcbuf;
int
net_try_send(char * data, int len)
{
//    cprintf("\n IN GUEST NET SEND :0x%x:\n", data, len);
    int ret = 0;
    uintptr_t tmp_adr = (uintptr_t) data;
    int num = VMX_VMCALL_NETSEND;
    uint64_t a1, a2;
/*
    if (data == NULL)
	return -E_INVAL;

    if ((uintptr_t)data >= UTOP)
	return -E_INVAL;
*/
//cprintf("DATA IS : 0x%x\n", data);
    if((vpml4e[VPML4E(tmp_adr)] & PTE_P) && (vpde[VPDPE(tmp_adr)] & PTE_P) && (vpd[VPD(tmp_adr)] & PTE_P)  &&  (vpt[VPN(tmp_adr)] & PTE_P))
    {
           data = (void *) PTE_ADDR( vpt[VPN(tmp_adr)] );
    }
    else
    {
	cprintf("\n Packet address not mapped SOMETHING IS REALLY WRONG \n");
	if((ret = sys_page_alloc(0, (void *) tmp_adr , PTE_P|PTE_W|PTE_U|PTE_SHARE)) < 0)
	    return ret;
        data = (void *) PTE_ADDR( vpt[VPN(tmp_adr)] );
    }
    
//    cprintf("DATA IS LEN IS: 0x%x:%d\n", data, len);
    a1 = (uint64_t) data; // the packet buffer ptr
    a2 = (uint64_t) len;


 //	return e1000_transmit(data, len);
// vmexit to host to send a packet

    asm volatile("vmcall\n"
		: "=a" (ret)
		: "a" (num),
		  "d" (a1),
		  "c" (a2)
		: "cc", "memory");
//cprintf("RET IN TRANSMIT _GUEST:%d:\n", ret);
    if (ret > 0)
	panic("vmcall %d returned %d (> 0) in GUEST NET_SEND", num, ret);
    return ret;
}


void
output(envid_t ns_envid)
{
	binaryname = "ns_output";
	char *buf=(char *)malloc(sizeof(char) * 2048);
	int len=0;

	memset(buf, 0, 2048);

	// LAB 6: Your code here:
	// 	- read a packet from the network server
	//	- send the packet to the device driver
	int r;
	while(1) {
		r = sys_ipc_recv(&nsipcbuf);
		if ( (thisenv->env_ipc_from != ns_envid) || (thisenv->env_ipc_value != NSREQ_OUTPUT)) {
			continue;
		}
		len=nsipcbuf.pkt.jp_len;
		memmove(buf, nsipcbuf.pkt.jp_data, nsipcbuf.pkt.jp_len);
		while ((r = net_try_send(buf, len)) != 0);
	}
	
}
