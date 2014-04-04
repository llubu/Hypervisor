#include <inc/lib.h>
#include <inc/vmx.h>
#include <inc/elf.h>
#include <inc/ept.h>
#define GUEST_KERN "/vmm/kernel"
#define GUEST_BOOT "/vmm/boot"

#define JOS_ENTRY 0x7000

// Map a region of file fd into the guest at guest physical address gpa.
// The file region to map should start at fileoffset and be length filesz.
// The region to map in the guest should be memsz.  The region can span multiple pages.
//
// Return 0 on success, <0 on failure.
//
static int
map_in_guest( envid_t guest, uintptr_t gpa, size_t memsz, 
        int fd, size_t filesz, off_t fileoffset ) {

    /* Your code here */
//    cprintf("gpa :%x", gpa);
//    cprintf("memsz %x", memsz);
//    cprintf("filesz %x\n", filesz);

    int i, r;
    void *blk;

    //cprintf("map_segment %x+%x\n", va, memsz);
    if ((i = PGOFF(gpa))) 
    {
	gpa -= i;
	memsz += i;
	filesz += i;
	fileoffset -= i;
    }

    int perm = PTE_P | PTE_U | PTE_W;

    for (i = 0; i < memsz; i += PGSIZE)
    {
	if (i >= filesz)
	{
	    // allocate a blank page
//	    if ((r = sys_page_alloc(guest, (void*) (gpa + i), perm)) < 0)
	    if ((r = sys_page_alloc(0, (void*) UTEMP, __EPTE_FULL)) < 0)
		return r;
//	    cprintf("FIRST::%d,%x,%d,%x::\n", thisenv->env_id, UTEMP, guest, (gpa + i));
	    if ((r = sys_ept_map(thisenv->env_id, UTEMP, guest, (void *)(gpa + i), __EPTE_FULL)) < 0)
	    {
		panic(": FIRST sys_ept_map data: %e", r);
	    }

	    sys_page_unmap(0, UTEMP);

      } 
      else
     {
	    // from file
	    if ((r = sys_page_alloc(0, UTEMP, PTE_P | PTE_U | PTE_W)) < 0)
		return r;
	    if ((r = seek(fd, fileoffset + i)) < 0)
		return r;
	    if ((r = readn(fd, UTEMP, MIN(PGSIZE, filesz-i))) < 0)
		return r;
//	    cprintf("::%d,%x,%d,%x::\n", thisenv->env_id, UTEMP, guest, (gpa + i));
	    if ((r = sys_ept_map(thisenv->env_id, UTEMP, guest, (void*) (gpa + i), __EPTE_FULL)) < 0)
	    {
		panic(": SECOND sys_ept_map data: %e", r);
	    }
	     
	    sys_page_unmap(0, UTEMP);
	}
    }
    return 0;
} 

// Read the ELF headers of kernel file specified by fname,
// mapping all valid segments into guest physical memory as appropriate.
//
// Return 0 on success, <0 on error
//
// Hint: compare with ELF parsing in env.c, and use map_in_guest for each segment.
static int
copy_guest_kern_gpa( envid_t guest, char* fname ) {

    /* Your code here */
unsigned char elf_buf[512];
int fd, i, r;
struct Elf *elf;
struct Proghdr *ph;
int perm;

if ((r = open(fname, O_RDONLY)) < 0)
    return r;
fd = r;

elf = (struct Elf*) elf_buf;
if (readn(fd, elf_buf, sizeof(elf_buf)) != sizeof(elf_buf)
	            || elf->e_magic != ELF_MAGIC) {
    close(fd);
//    cprintf("elf magic %08x want %08x\n", elf->e_magic, ELF_MAGIC);
    return -E_NOT_EXEC;
}

ph = (struct Proghdr*) (elf_buf + elf->e_phoff);
for (i = 0; i < elf->e_phnum; i++, ph++) {
    if (ph->p_type != ELF_PROG_LOAD)
	continue;
    
//    printf("\n ph %d",ph->p_type);
    perm = PTE_P | PTE_U;
    if (ph->p_flags & ELF_PROG_FLAG_WRITE)
	perm |= PTE_W;
    if ((r = map_in_guest(guest, ph->p_pa, ph->p_memsz,
		           fd, ph->p_filesz, ph->p_offset)) < 0)
	goto error;
}
close(fd);
fd = -1;

return r;

error:
//sys_env_destroy(child);
    close(fd);
    return -E_NO_SYS;
}

void
umain(int argc, char **argv) {
    int ret;
    envid_t guest;

//    cprintf("\n IN USER VMM \n");

    if ((ret = sys_env_mkguest( GUEST_MEM_SZ, JOS_ENTRY )) < 0) {
        cprintf("Error creating a guest OS env: %e\n", ret );
        exit();
    }
    guest = ret;
cprintf("\n AFTER CREATING VMM ENV \n");
    // Copy the guest kernel code into guest phys mem.
    if((ret = copy_guest_kern_gpa(guest, GUEST_KERN)) < 0) {
	cprintf("Error copying page into the guest - %d\n.", ret);
        exit();
    }
cprintf("BOOT%d\n",__LINE__);
    // Now copy the bootloader.
    int fd;
    if ((fd = open( GUEST_BOOT, O_RDONLY)) < 0 ) {
        cprintf("open %s for read: %e\n", GUEST_BOOT, fd );
        exit();
    }

    // sizeof(bootloader) < 512.
    if ((ret = map_in_guest(guest, JOS_ENTRY, 512, fd, 512, 0)) < 0) {
	cprintf("Error mapping bootloader into the guest - %d\n.", ret);
	exit();
    }
//    cprintf("\n BOOTLOADER DONE \n");
    // Mark the guest as runnable.
    sys_env_set_status(guest, ENV_RUNNABLE);
    wait(guest);
}


