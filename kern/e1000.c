#include <kern/e1000.h>

struct tx_desc tx_desc_array[E1000_TXDESCSZ] __attribute__((aligned(16)));
struct tx_pkt tx_pkt_bufs[E1000_TXDESCSZ];

struct rcv_desc rcv_desc_array[E1000_RCVDESCSZ] __attribute__ ((aligned (16)));
struct rcv_pkt rcv_pkt_bufs[E1000_RCVDESCSZ];

// Guest pkt buffer
struct rcv_desc guest_rcv_desc_array[E1000_RCVDESCSZ] __attribute__ ((aligned (16)));	// Guest desc for receiving -- cant do only with the buffer
struct rcv_pkt guest_rcv_pkt_bufs[E1000_RCVDESCSZ]; // 64 -each of size 2048
int guest_hd = 0;	// head counter for guest buf
int guest_tl = 0;	// tail counter for guest buf

// LAB 6: Your driver code here
int e1000_attach (struct pci_func *f) {
	
	int i;
	
	//enable the E1000 PCI device
	pci_func_enable(f);
	//Map the registers in memory
	e1000 = mmio_map_region(f->reg_base[0], f->reg_size[0]);
	//check that the mapping is right
	assert(e1000[E1000_STATUS] == 0x80080783);
	// Initialize tx buffer array - zeroing out the arrays
	memset(tx_desc_array, 0x0, sizeof(struct tx_desc) * E1000_TXDESCSZ);
	memset(tx_pkt_bufs, 0x0, sizeof(struct tx_pkt) * E1000_TXDESCSZ);
	for (i = 0; i < E1000_TXDESCSZ; i++) {
		tx_desc_array[i].addr = PADDR(tx_pkt_bufs[i].buf);
		tx_desc_array[i].status |= E1000_TXD_STAT_DD;
	}
	
	//Program the Transmit Descriptor Base Address (TDBAL/TDBAH) register(s) with the address of the region
	e1000[E1000_TDBAL] = PADDR(tx_desc_array) & 0x00000000ffffffff;
	e1000[E1000_TDBAH] = PADDR(tx_desc_array) & 0x0000ffff00000000;
	
	//Set the TDLEN register to the size (in bytes) of the descriptor ring.
	e1000[E1000_TDLEN] = sizeof(struct tx_desc) * E1000_TXDESCSZ;
	
	//The Transmit Descriptor Head and Tail (TDH/TDT) registers should be initialized to 0.
	e1000[E1000_TDH] = 0;
	e1000[E1000_TDT] = 0;
	
	//Set the Enable (TCTL.EN) bit to 1b for normal operation
	e1000[E1000_TCTL] |= E1000_TCTL_EN;
	e1000[E1000_TCTL] |= E1000_TCTL_PSP;
	e1000[E1000_TCTL] &= ~E1000_TCTL_COLD;
	e1000[E1000_TCTL] |= (0x40) << 12;
	
	//Program the Transmit IPG (TIPG) register
	e1000[E1000_TIPG] = 0x0;
	e1000[E1000_TIPG] |= (0x6) << 20; // IPGR2 
	e1000[E1000_TIPG] |= (0x4) << 10; // IPGR1
	e1000[E1000_TIPG] |= 0xA; // IPGR	
	
	memset(rcv_desc_array, 0x0, sizeof(struct rcv_desc) * E1000_RCVDESCSZ);
	memset(rcv_pkt_bufs, 0x0, sizeof(struct rcv_pkt) * E1000_RCVDESCSZ);
	for (i = 0; i < E1000_RCVDESCSZ; i++) {
		rcv_desc_array[i].addr = PADDR(rcv_pkt_bufs[i].buf);
		guest_rcv_desc_array[i].addr = PADDR(guest_rcv_pkt_bufs[i].buf);	// Initializing Guest desc also
		guest_rcv_desc_array[i].length = 0;
		guest_rcv_desc_array[i].status = 0;
		guest_rcv_desc_array[i].pktcsum = 0;
		guest_rcv_desc_array[i].errors = 0;
		guest_rcv_desc_array[i].special = 0;
	}	
	
	//Program the Receive Address Register(s) (RAL/RAH) with the desired Ethernet addresses
	e1000[E1000_RAL] = 0x12005452;
	e1000[E1000_RAH] = 0x80005634;
	
	
	//Program the Receive Descriptor Base Address Registers
	e1000[E1000_RDBAL] = PADDR(rcv_desc_array) & 0x00000000ffffffff;
	e1000[E1000_RDBAH] = PADDR(rcv_desc_array) & 0x0000ffff00000000;

	//Set the Receive Descriptor Length Register
	e1000[E1000_RDLEN] = sizeof(struct rcv_desc) * E1000_RCVDESCSZ;

	//Set the Receive Descriptor Head and Tail Registers
	e1000[E1000_RDH] = 0;
	e1000[E1000_RDT] = 0;

	//Initialize the Receive Control Register
	e1000[E1000_RCTL] |= E1000_RCTL_EN;
	e1000[E1000_RCTL] &= ~E1000_RCTL_LPE;
	e1000[E1000_RCTL] &= ~E1000_RCTL_LBM;
	e1000[E1000_RCTL] &= ~E1000_RCTL_RDMTS;
	e1000[E1000_RCTL] &= ~E1000_RCTL_MO;
	e1000[E1000_RCTL] |= E1000_RCTL_BAM;
	e1000[E1000_RCTL] &= ~E1000_RCTL_SZ; // 2048 byte size
	e1000[E1000_RCTL] |= E1000_RCTL_SECRC;

	return 0;
}


int
e1000_transmit(char *data, int len)
{
	if (len > E1000_TXPCKTSZ) {
		return -E_PKT_TOO_LONG;
	}
//	cprintf("\n IN E1000 transmit \n");
//	cprintf("\n DATA :0x%x:%d\n", data, len);
	uint32_t tdt = e1000[E1000_TDT];
	// Check if next tx_desc is free
	if (tx_desc_array[tdt].status & E1000_TXD_STAT_DD) {
		memmove(tx_pkt_bufs[tdt].buf, data, len);
		tx_desc_array[tdt].length = len;

		tx_desc_array[tdt].status &= ~E1000_TXD_STAT_DD;		// Clear DD so that we can use it to check whether the packet got sent
		tx_desc_array[tdt].cmd |= E1000_TXD_CMD_RS;			// to get the card to set dd when done sending
		tx_desc_array[tdt].cmd |= E1000_TXD_CMD_EOP;			// to indicate last packet
//cprintf("DABRAL:%d:%s\n", __LINE__, __FILE__);
		e1000[E1000_TDT] = (tdt + 1) % E1000_TXDESCSZ;
	}
	else { // tx queue is full!
		return -E_TX_FULL;
	}
	return 0;
}

int
e1000_receive(char *data)
{
	uint32_t rdt, len, i = 0;
	rdt = e1000[E1000_RDT];
//	cprintf("\n THE GEUST HEAD IN E1000_RCV is:%d\n", guest_hd);
//	cprintf("\n IN E1000 RECEIVE \n");
	//if next rcvdesc is filled
//	guest_rcv_desc_array[guest_hd].status = rcv_desc_array[rdt].status;
	if (rcv_desc_array[rdt].status & E1000_RXD_STAT_DD) {
		
//	guest_rcv_desc_array[guest_hd].status = rcv_desc_array[rdt].status;
		if (!(rcv_desc_array[rdt].status & E1000_RXD_STAT_EOP)) {
			panic("We don't allow jumbo frames!\n");
		}
		
		guest_rcv_desc_array[guest_hd].status = rcv_desc_array[rdt].status;
		len = rcv_desc_array[rdt].length;
		guest_rcv_desc_array[guest_hd].length = rcv_desc_array[rdt].length;			//copying the length to the guest desc
		memmove(data, rcv_pkt_bufs[rdt].buf, len);
		// Copying the pkt into guest pkt buf
		memmove(guest_rcv_pkt_bufs[guest_hd].buf, rcv_pkt_bufs[rdt].buf, len);
//                cprintf("\n DATA od LEN=[%d]=[",guest_rcv_desc_array[guest_hd].length);
//		for(i = 0;i < guest_rcv_desc_array[guest_hd].length; i++)
//		     cprintf("%u ",guest_rcv_pkt_bufs[guest_hd].buf[i]);
//		cprintf("]\n");
//		cprintf("\n HOST BUF OKT DATA \n");
//		for(i = 0;i < rcv_desc_array[rdt].length; i++)
//		     cprintf("%u ",data[i]);
//		cprintf("]\n");
		
		rcv_desc_array[rdt].status &= ~E1000_RXD_STAT_DD;
		rcv_desc_array[rdt].status &= ~E1000_RXD_STAT_EOP;
		e1000[E1000_RDT] = (rdt + 1) % E1000_RCVDESCSZ;

		if (guest_hd == 63)
		    guest_hd = 0;
		else
		    ++guest_hd;

		return len;
	}
	//no packets found.
	return -E_RCV_EMPTY;
}


int 
guest_rcv(char *data, int *rlen, int *rret)
{
    int len = 0,i=0;
    char *g_src = NULL;

 //   cprintf("\n GUEST HEAD AND TAILS IN GUEST RCV:%d:%d\n", guest_hd, guest_tl);

    if (guest_rcv_desc_array[guest_tl].status & E1000_RXD_STAT_DD) 
    {
	if (!(guest_rcv_desc_array[guest_tl].status & E1000_RXD_STAT_EOP)) 
	{
	    panic("We don't allow jumbo frames in guest_RCV!\n");
	}

    	len = guest_rcv_desc_array[guest_tl].length;
	memmove(data, guest_rcv_pkt_bufs[guest_tl].buf, len);
        g_src = (char *) guest_rcv_pkt_bufs[guest_tl].buf;	
//	for(i = 0; i < guest_rcv_desc_array[guest_tl].length; i++)
//	{
//	    data[i] = guest_rcv_pkt_bufs[guest_tl].buf[i];
//	    data[i] = g_src[i];
//	}

//        cprintf("\n DATA-GUEST od LEN=[%d]=[",guest_rcv_desc_array[guest_tl].length);
//	for(i=0;i<guest_rcv_desc_array[guest_tl].length;i++)
//	     cprintf("%u ",guest_rcv_pkt_bufs[guest_tl].buf[i]);
//	cprintf("]\n");
//    	cprintf("\n CHEK PRINT IN GUEST BUF \n");
//	for(i=0;i<guest_rcv_desc_array[guest_tl].length;i++)
//	     cprintf("%u ",data[i]);
//	cprintf("]\n");

//   	guest_rcv_desc_array[guest_tl].status &= ~E1000_RXD_STAT_DD;
//    	guest_rcv_desc_array[guest_tl].status &= ~E1000_RXD_STAT_EOP;
	guest_rcv_desc_array[i].length = 0;
	guest_rcv_desc_array[i].status = 0;
	guest_rcv_desc_array[i].pktcsum = 0;
	guest_rcv_desc_array[i].errors = 0;
	guest_rcv_desc_array[i].special = 0;

	if (guest_tl == 63)
		guest_tl = 0;
    	else
		++guest_tl;
	*rlen = len;
	*rret = 0;
     	return 0;
    }
    *rlen = (int) 0;
    *rret =(int) -E_RCV_EMPTY;

    return -E_RCV_EMPTY;
}
