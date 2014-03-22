
#include "fs.h"

// Return the virtual address of this disk block.
    void*
diskaddr(uint64_t blockno)
{
    if (blockno == 0 || (super && blockno >= super->s_nblocks))
        panic("bad block number %08x in diskaddr", blockno);
    return (char*) (DISKMAP + blockno * BLKSIZE);
}

// Is this virtual address mapped?
    bool
va_is_mapped(void *va)
{
    return (vpml4e[VPML4E(va)] & PTE_P) && (vpde[VPDPE(va)] & PTE_P) && (vpd[VPD(va)] & PTE_P) && (vpt[PPN(va)] & PTE_P);
}

// Is this virtual address dirty?
    bool
va_is_dirty(void *va)
{
    return (vpt[PPN(va)] & PTE_D) != 0;
}

// Fault any disk block that is read or written in to memory by
// loading it from disk.
// Hint: Use ide_read and BLKSECTS.
    static void
bc_pgfault(struct UTrapframe *utf)
{
    void *addr = (void *) utf->utf_fault_va;
    uint64_t blockno = ((uint64_t)addr - DISKMAP) / BLKSIZE;
    int r;

    // Check that the fault was within the block cache region
    if (addr < (void*)DISKMAP || addr >= (void*)(DISKMAP + DISKSIZE))
        panic("page fault in FS: eip %08x, va %08x, err %04x",
                utf->utf_rip, addr, utf->utf_err);

    // Sanity check the block number.
    if (super && blockno >= super->s_nblocks)
        panic("reading non-existent block %08x\n", blockno);

    // Allocate a page in the disk map region, read the contents
    // of the block from the disk into that page, and mark the
    // page not-dirty (since reading the data from disk will mark
    // the page dirty).
    //
    // LAB 5: Your code here
    void *block_addr = ROUNDDOWN(addr, BLKSIZE);
    if((r = sys_page_alloc(thisenv->env_id, block_addr, PTE_SYSCALL)) < 0)
	panic("bc_pgfault:failed(err = %d) to map page at block %d\n",r,blockno);

    if((r = ide_read(BLKSECTS * blockno, block_addr, BLKSECTS) < 0))
	panic("bc_pgfault:failed to read the sector\n");

    if((r = sys_page_map(thisenv->env_id, block_addr, thisenv->env_id, block_addr, PTE_SYSCALL)) < 0)
	panic("bc_pgfault:failed to map the page back as not-dirty\n");


    // Check that the block we read was allocated. (exercise for
    // the reader: why do we do this *after* reading the block
    // in?)
    if (bitmap && block_is_free(blockno))
        panic("reading free block %08x\n", blockno);
}

// Flush the contents of the block containing VA out to disk if
// necessary, then clear the PTE_D bit using sys_page_map.
// If the block is not in the block cache or is not dirty, does
// nothing.
// Hint: Use va_is_mapped, va_is_dirty, and ide_write.
// Hint: Use the PTE_SYSCALL constant when calling sys_page_map.
// Hint: Don't forget to round addr down.
    void
flush_block(void *addr)
{
    uint64_t blockno = ((uint64_t)addr - DISKMAP) / BLKSIZE;

    if (addr < (void*)DISKMAP || addr >= (void*)(DISKMAP + DISKSIZE))
        panic("flush_block of bad va %08x", addr);

    // LAB 5: Your code here.
    int r;
    void *block_addr = ROUNDDOWN(addr, BLKSIZE);
    if (va_is_mapped(block_addr) && va_is_dirty(block_addr))
    {
	if((r = ide_write(BLKSECTS * blockno, block_addr, BLKSECTS) < 0))
	    panic("flush_block:failed to write the sector\n");
	if((r = sys_page_map(thisenv->env_id, block_addr, thisenv->env_id, block_addr, PTE_SYSCALL) < 0))
	    panic("flush_block:failed to map the page as not-dirty\n");
    }

}

// Test that the block cache works, by smashing the superblock and
// reading it back.
    static void
check_bc(void)
{
    struct Super backup;

    // back up super block
    memmove(&backup, diskaddr(1), sizeof backup);

    // smash it
    strcpy(diskaddr(1), "OOPS!\n");
    flush_block(diskaddr(1));
    assert(va_is_mapped(diskaddr(1)));
    assert(!va_is_dirty(diskaddr(1)));

    // clear it out
    sys_page_unmap(0, diskaddr(1));
    assert(!va_is_mapped(diskaddr(1)));

    // read it back in
    assert(strcmp(diskaddr(1), "OOPS!\n") == 0);

    // fix it
    memmove(diskaddr(1), &backup, sizeof backup);
    flush_block(diskaddr(1));

    cprintf("block cache is good\n");
}

    void
bc_init(void)
{
    set_pgfault_handler(bc_pgfault);
    check_bc();
}

