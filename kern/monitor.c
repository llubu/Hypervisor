// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>
#include <kern/trap.h>
#include <kern/pmap.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "backtrace", "Display stack backtrace", mon_backtrace },
	{ "showcolor", "Display colored test", show_color },
	{ "setconsolecolor", "Set color of console: black; red; green; yellow; blue; magenta; cyan; white;", set_console_color},
	{ "showmappings", "Show the virtual to physical mappings", mon_showmappings},
	{ "dump", "Show the contents at virtual address", mon_dumpmemcontents},
	{ "changeperm", "Change the permissions of page at particular virtual address", mon_changepermissions},
	{ "statpages", "Stat the mapped pages to display number of read/write/present pages", mon_statpages}
};

#define NCOMMANDS (sizeof(commands)/sizeof(commands[0]))

/***** Implementations of basic kernel monitor commands *****/

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < NCOMMANDS; i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char _start[], entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start                  %08x (phys)\n", _start);
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		ROUNDUP(end - entry, 1024) / 1024);
	return 0;
}

int
show_color(int argc, char **argv, struct Trapframe *tf)
{
	cprintf("\e[0;34mThis is colored text.\e[m \e[1;32mThis is colored bold text.\e[m \e[4;35mThis is underlined text.\e[m\n");
	return 1;
}

int
set_console_color(int argc, char **argv, struct Trapframe *tf)
{
	if(argc != 2)
	{
		cprintf("Invalid parameters\n");
		return 0;
	}
	else if(!strcmp(argv[1],"black"))
	{
		cprintf("\e[0;30m");
		return 1;
	}
        else if(!strcmp(argv[1],"red"))
        {
                cprintf("\e[0;31m");
                return 1;
	}
        else if(!strcmp(argv[1],"green"))
        {
                cprintf("\e[0;32m");
                return 1;
	}
        else if(!strcmp(argv[1],"yellow"))
        {
                cprintf("\e[0;33m");
                return 1;
	}
        else if(!strcmp(argv[1],"blue"))
        {
                cprintf("\e[0;34m");
                return 1;
	}
       	else if(!strcmp(argv[1],"magenta"))
        {
                cprintf("\e[0;35m");
                return 1;
	}
        else if(!strcmp(argv[1],"cyan"))
        {
                cprintf("\e[0;36m");
                return 1;
        }
        else
        {
                cprintf("\e[0;37m");
                return 1;
        }
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	uint64_t rip, argument1, argument2, argument3, argument4;
	uint64_t *current_rbp;

	cprintf("\e[1;4;36mStack backtrace:\e[m\n");

	current_rbp = (uint64_t*)read_rbp();
	while(current_rbp != 0) {
		rip = *(current_rbp + 1);
		argument1 = *(current_rbp - 1);
		argument2 = *(current_rbp - 2);
		argument3 = *(current_rbp - 3);
		argument4 = *(current_rbp - 4);
		uint32_t *ptr32 = (uint32_t*)(current_rbp);
		
		struct Ripdebuginfo info;
		int success = debuginfo_rip(rip, &info);

		cprintf("  rbp %016x  rip %016x  args %016x %016x %016x %016x\n",current_rbp, rip, *(ptr32 - 1), *(ptr32 -2), *(ptr32 -3), *(ptr32 -4));
		if(success == 0)
			cprintf("         \e[1;32m../file=\e[m%s:%d: %.*s+%016x\n", info.rip_file, info.rip_line, info.rip_fn_namelen, info.rip_fn_name, rip - info.rip_fn_addr); 
		
		current_rbp = (uint64_t*)(*current_rbp);
	}
	return 0;
}

void
show_permissions(pte_t *pte)
{
	if (pte == NULL)
		panic("NULL pointer\n");

	if(*pte & PTE_P)
	{
		cprintf("PTE_P ");
	}
	if(*pte & PTE_W)
	{
		cprintf("PTE_W ");
	}	
	if(*pte & PTE_U)
	{
		cprintf("PTE_U ");
	}	
	if(*pte & PTE_PWT)
	{
		cprintf("PTE_PWT ");
	}	
	if(*pte & PTE_PCD)
	{
		cprintf("PTE_PCD ");
	}	
	if(*pte & PTE_A)
	{
		cprintf("PTE_A ");
	}	
	if(*pte & PTE_D)
	{
		cprintf("PTE_D");
	}	

	cprintf("\n");
}

int
mon_showmappings(int argc, char **argv, struct Trapframe *tf)
{
	if(argc != 3)
		panic("Invalid Arguments. There should be two arguments: start_va and end_va\n");
	else
	{
		uint64_t va = 0;
		uint64_t vastart;
		uint64_t vaend;
		char* end;

		vastart = ROUNDDOWN((uint64_t)(strtol(argv[1], &end, 16)), PGSIZE);
		vaend = ROUNDUP((uint64_t)(strtol(argv[2], &end, 16)), PGSIZE);
			
		cprintf("Virtual Addr | Physical Addr | Permissions\n");
		cprintf("------------------------------------------\n");
		for (va = vastart; va < vaend; va+=PGSIZE)
		{
			cprintf("0x%08x   | ",va);
			pte_t *pte = pml4e_walk(boot_pml4e, (void *)va, 0);
			if (pte == NULL)
				cprintf("NULL          |      NULL\n");
			else
			{
				cprintf("0x%08x    | ",PTE_ADDR(*pte));
				show_permissions(pte);
			}
		}
	}
			
	return 0;
}

int
mon_changepermissions(int argc, char **argv, struct Trapframe *tf)
{
        if(argc < 4)
                panic("Invalid Arguments. There should be three arguments: set/clear, virtual address, permission flags\n");
        else
        {
                uint64_t va = 0;
                char* end;
		int perm = 0;
		uint64_t flags = 0;

		flags = (uint64_t)strtol(argv[3], &end, 16);
		
                va = ROUNDDOWN((uint64_t)(strtol(argv[2], &end, 16)), PGSIZE);

		pte_t *pte = pml4e_walk(boot_pml4e, (void *)va, 0);
                if (pte == NULL)
                	cprintf("No physical mappings exist at this address");
                else
		{
			cprintf("Before changing permissions, pte = 0x%x\n",*pte);

			if (!strcmp(argv[1], "set"))
				*pte |= flags;
			else if (!strcmp(argv[1], "clear"))
				*pte &= ~flags;
			
			cprintf("After changing permissiions, pte = 0x%x\n",*pte);
		}
                
        }

        return 0;
}

int
mon_dumpmemcontents(int argc, char **argv, struct Trapframe *tf)
{
        if(argc != 3)
                panic("Invalid Arguments. There should be two arguments stating start and end of physical mappings\n");
        else
        {
                uint64_t *va = NULL;
                uint64_t vastart;
                uint64_t vaend;
                char* end;

                vastart = (uint64_t)(strtol(argv[1], &end, 16));
                vaend = (uint64_t)(strtol(argv[2], &end, 16));

                for (va = (uint64_t*)vastart; va < (uint64_t*)vaend; va+=4) {
			cprintf("0x%08x  |  0x%08x  |  0x%08x  |  0x%08x\n",va[0],va[1],va[2],va[3]);
                }
        }

        return 0;
}

int
mon_statpages(int argc, char **argv, struct Trapframe *tf)
{
	if(argc != 3)
                panic("Invalid Arguments. There should be two arguments: start_va and end_va\n");
        else
        {
                uint64_t va = 0;
                uint64_t vastart;
                uint64_t vaend;
		size_t total=0, present=0, write=0, read=0, user=0, accessed=0, dirty=0;
                char* end;

                vastart = ROUNDDOWN((uint64_t)(strtol(argv[1], &end, 16)), PGSIZE);
                vaend = ROUNDUP((uint64_t)(strtol(argv[2], &end, 16)), PGSIZE);

                for (va = vastart; va < vaend; va+=PGSIZE)
                {
                        pte_t *pte = pml4e_walk(boot_pml4e, (void *)va, 0);
                        if (pte != NULL) {
				total++;
				if (*pte & PTE_P)
					present++;
				if (*pte & PTE_W)
					write++;
				else
					read++;
				if (*pte & PTE_U)
					user++;
				if (*pte & PTE_A)
					accessed++;
				if (*pte & PTE_D)
					dirty++;
			}
		}
		cprintf("Total pages = %u\nPTE_P = %u\nPTE_W = %u\n!PTE_W = %u\nPTE_U = %u\nPTE_A = %u\nPTE_D = %u\n"
				, total, present, write, read, user, accessed, dirty);
	}

        return 0;	
}


/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < NCOMMANDS; i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");

	if (tf != NULL)
		print_trapframe(tf);

	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}
