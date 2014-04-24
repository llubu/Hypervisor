// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/dwarf.h>
#include <kern/kdebug.h>
#include <kern/dwarf_api.h>
#include <kern/trap.h>

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
