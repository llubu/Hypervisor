#include "ns.h"

extern union Nsipc nsipcbuf;
extern struct rcv_pkt guest_rcv_pkt_bufs;	//defined in host-kern/e1000.c
extern int guest_hd;
extern int guest_tl;

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
	char buf[2048];

	int len, r, i;

	while (1) {
		// 	- read a packet from the device driver
		while ((r = sys_net_try_receive(buf, &len)) < 0)
			sys_yield();

		// Whenever a new page is allocated, old will be deallocated by page_insert automatically.
		while ((r = sys_page_alloc(0, &nsipcbuf, PTE_U | PTE_P | PTE_W)) < 0);

		nsipcbuf.pkt.jp_len = len;
		memmove(nsipcbuf.pkt.jp_data, buf, len);

		while ((r = sys_ipc_try_send(ns_envid, NSREQ_INPUT, &nsipcbuf, PTE_P | PTE_W | PTE_U)) < 0);
	}
}
