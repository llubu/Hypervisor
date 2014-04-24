#include <inc/mmu.h>
#include <inc/x86.h>
#include <inc/assert.h>

#include <kern/pmap.h>
#include <kern/trap.h>
#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/env.h>
#include <kern/syscall.h>
#include <kern/sched.h>
#include <kern/kclock.h>
#include <kern/picirq.h>
#include <kern/cpu.h>
#include <kern/spinlock.h>
#include <kern/time.h>

extern uintptr_t gdtdesc_64;
static struct Taskstate ts;
extern struct Segdesc gdt[];
extern long gdt_pd;

/* For debugging, so print_trapframe can distinguish between printing
 * a saved trapframe and printing the current trapframe and print some
 * additional information in the latter case.
 */
static struct Trapframe *last_tf;

/* Interrupt descriptor table.  (Must be built at run time because
 * shifted function addresses can't be represented in relocation records.)
 */
struct Gatedesc idt[256] = { { 0 } };
struct Pseudodesc idt_pd = {0,0};


static const char *trapname(int trapno)
{
	static const char * const excnames[] = {
		"Divide error",					//0
		"Debug",							//1
		"Non-Maskable Interrupt",			//2				
		"Breakpoint",						//3	
		"Overflow",						//4	
		"BOUND Range Exceeded",				//5			
		"Invalid Opcode",					//6		
		"Device Not Available",				//7		
		"Double Fault",					//8		
		"Coprocessor Segment Overrun",		//9				
		"Invalid TSS",						//10
		"Segment Not Present",				//11		
		"Stack Fault",						//12
		"General Protection",				//13		
		"Page Fault",						//14
		"(unknown trap)",					//15	
		"x87 FPU Floating-Point Error",		//16				
		"Alignment Check",					//17	
		"Machine-Check",					//18		
		"SIMD Floating-Point Exception"		//19				
	};

	if (trapno < sizeof(excnames)/sizeof(excnames[0]))
		return excnames[trapno];
	if (trapno == T_SYSCALL)
		return "System call";
	if (trapno >= IRQ_OFFSET && trapno < IRQ_OFFSET + 16)
		return "Hardware Interrupt";
	return "(unknown trap)";
}


//Initialise IDT here.
// Set up a normal interrupt/trap gate descriptor.
// - istrap: 1 for a trap (= exception) gate, 0 for an interrupt gate.
    //   see section 9.6.1.3 of the i386 reference: "The difference between
    //   an interrupt gate and a trap gate is in the effect on IF (the
    //   interrupt-enable flag). An interrupt that vectors through an
    //   interrupt gate resets IF, thereby preventing other interrupts from
    //   interfering with the current interrupt handler. A subsequent IRET
    //   instruction restores IF to the value in the EFLAGS image on the
    //   stack. An interrupt through a trap gate does not change IF."
// - sel: Code segment selector for interrupt/trap handler
// - off: Offset in code segment for interrupt/trap handler
// - dpl: Descriptor Privilege Level -
//	  the privilege level required for software to invoke
//	  this interrupt/trap gate explicitly using an int instruction.
void
trap_init(void)
{
	extern struct Segdesc gdt[];

	// LAB 3: Your code here.
	extern void trap_divide();
	extern void trap_debug();
	extern void trap_nmi();
	extern void trap_brkpt();
	extern void trap_oflow();
	extern void trap_bound();
	extern void trap_illop();
	extern void trap_device();
	extern void trap_dblflt();
	extern void trap_tss();
	extern void trap_segnp();
	extern void trap_stack();
	extern void trap_gpflt();
	extern void trap_pgflt();
	extern void trap_fperr();
	extern void trap_align();
	extern void trap_mchk();
	extern void trap_simderr();
	extern void trap_syscall();
	
	extern void trap_IRQ0();
	extern void trap_IRQ1();
	extern void trap_IRQ2();
	extern void trap_IRQ3();
	extern void trap_IRQ4();
	extern void trap_IRQ5();
	extern void trap_IRQ6();
	extern void trap_IRQ7();
	extern void trap_IRQ8();
	extern void trap_IRQ9();
	extern void trap_IRQ10();
	extern void trap_IRQ11();
	extern void trap_IRQ12();
	extern void trap_IRQ13();
	extern void trap_IRQ14();
	extern void trap_IRQ15();	
	
	//1101111111111111111
	
	SETGATE(idt[T_DIVIDE], 0, GD_KT, trap_divide, 0);
	SETGATE(idt[T_DEBUG], 0, GD_KT, trap_debug, 0);
	SETGATE(idt[T_NMI], 0, GD_KT, trap_nmi, 0);
	SETGATE(idt[T_BRKPT], 0, GD_KT, trap_brkpt, 3);
	SETGATE(idt[T_OFLOW], 0, GD_KT, trap_oflow, 0);
	SETGATE(idt[T_BOUND], 0, GD_KT, trap_bound, 0);
	SETGATE(idt[T_ILLOP], 0, GD_KT, trap_illop, 0);
	SETGATE(idt[T_DEVICE], 0, GD_KT, trap_device, 0);
	SETGATE(idt[T_DBLFLT], 0, GD_KT, trap_dblflt, 0);
	SETGATE(idt[T_TSS], 0, GD_KT, trap_tss, 0);
	SETGATE(idt[T_SEGNP], 0, GD_KT, trap_segnp, 0);
	SETGATE(idt[T_STACK], 0, GD_KT, trap_stack, 0);
	SETGATE(idt[T_GPFLT], 0, GD_KT, trap_gpflt, 0);
	SETGATE(idt[T_PGFLT], 0, GD_KT, trap_pgflt, 0);
	SETGATE(idt[T_FPERR], 0, GD_KT, trap_fperr, 0);
	SETGATE(idt[T_ALIGN], 0, GD_KT, trap_align, 0);
	SETGATE(idt[T_MCHK], 0, GD_KT, trap_mchk, 0);
	SETGATE(idt[T_SIMDERR], 0, GD_KT, trap_simderr, 0);
	
	SETGATE(idt[T_SYSCALL], 0, GD_KT, trap_syscall, 3);
	
	SETGATE(idt[T_IRQ0], 0, GD_KT,  trap_IRQ0, 0);
	SETGATE(idt[T_IRQ1], 0, GD_KT,  trap_IRQ1, 0);
	SETGATE(idt[T_IRQ2], 0, GD_KT,  trap_IRQ2, 0);
	SETGATE(idt[T_IRQ3], 0, GD_KT,  trap_IRQ3, 0);
	SETGATE(idt[T_IRQ4], 0, GD_KT,  trap_IRQ4, 0);
	SETGATE(idt[T_IRQ5], 0, GD_KT,  trap_IRQ5, 0);
	SETGATE(idt[T_IRQ6], 0, GD_KT,  trap_IRQ6, 0);
	SETGATE(idt[T_IRQ7], 0, GD_KT,  trap_IRQ7, 0);
	SETGATE(idt[T_IRQ8], 0, GD_KT,  trap_IRQ8, 0);
	SETGATE(idt[T_IRQ9], 0, GD_KT,  trap_IRQ9, 0);
	SETGATE(idt[T_IRQ10], 0, GD_KT,  trap_IRQ10, 0);
	SETGATE(idt[T_IRQ11], 0, GD_KT,  trap_IRQ11, 0);
	SETGATE(idt[T_IRQ12], 0, GD_KT,  trap_IRQ12, 0);
	SETGATE(idt[T_IRQ13], 0, GD_KT,  trap_IRQ13, 0);
	SETGATE(idt[T_IRQ14], 0, GD_KT,  trap_IRQ14, 0);
	SETGATE(idt[T_IRQ15], 0, GD_KT,  trap_IRQ15, 0);
		
	//cprintf("%x\n",);
	idt_pd.pd_lim = sizeof(idt)-1;
	idt_pd.pd_base = (uint64_t)idt;
	// Per-CPU setup
	trap_init_percpu();
}

// Initialize and load the per-CPU TSS and IDT
void
trap_init_percpu(void)
{
	// The example code here sets up the Task State Segment (TSS) and
	// the TSS descriptor for CPU 0. But it is incorrect if we are
	// running on other CPUs because each CPU has its own kernel stack.
	// Fix the code so that it works for all CPUs.
	//
	// Hints:
	//   - The macro "thiscpu" always refers to the current CPU's
	//     struct Cpu;
	//   - The ID of the current CPU is given by cpunum() or
	//     thiscpu->cpu_id;
	//   - Use "thiscpu->cpu_ts" as the TSS for the current CPU,
	//     rather than the global "ts" variable;
	//   - Use gdt[(GD_TSS0 >> 3) + 2*i] for CPU i's TSS descriptor;
	//   - You mapped the per-CPU kernel stacks in mem_init_mp()
	//
	// ltr sets a 'busy' flag in the TSS selector, so if you
	// accidentally load the same TSS on more than one CPU, you'll
	// get a triple fault.  If you set up an individual CPU's TSS
	// wrong, you may not get a fault until you try to return from
	// user space on that CPU.
	//
	// LAB 4: Your code here:

	// Setup a TSS so that we get the right stack
	// when we trap to the kernel.
	//thiscpu->cpu_ts.ts_esp0 = KSTACKTOP-(thiscpu->cpu_id*(KSTKSIZE+KSTKGAP));
	thiscpu->cpu_ts.ts_esp0 = KSTACKTOP - cpunum() * (KSTKSIZE + KSTKGAP);
	 
	// Initialize the TSS slot of the gdt.
	//SETTSS((struct SystemSegdesc64 *)((gdt_pd>>16)+40)+thiscpu->cpu_id,STS_T64A, (uint64_t)(&(thiscpu->cpu_ts)),sizeof(struct Taskstate), 0);
	SETTSS((struct SystemSegdesc64 *)(&gdt[(GD_TSS0 >> 3) + 2*thiscpu->cpu_id]),STS_T64A, (uint64_t)(&(thiscpu->cpu_ts)),sizeof(struct Taskstate), 0);	// CHECK HERE!! MAYBE WRONG!!
	// Load the TSS selector (like other segment selectors, the
	// bottom three bits are special; we leave them 0)
	//cprintf("hither");	
	ltr(GD_TSS0 + 2*(thiscpu->cpu_id<<3));

	// Load the IDT
	lidt(&idt_pd);
	//cprintf("IDT initialised\n");
}

void
print_trapframe(struct Trapframe *tf)
{
	cprintf("TRAP frame at %p from CPU %d\n", tf, cpunum());
	print_regs(&tf->tf_regs);
	cprintf("  es   0x----%04x\n", tf->tf_es);
	cprintf("  ds   0x----%04x\n", tf->tf_ds);
	cprintf("  trap 0x%08x %s\n", tf->tf_trapno, trapname(tf->tf_trapno));
	// If this trap was a page fault that just happened
	// (so %cr2 is meaningful), print the faulting linear address.
	if (tf == last_tf && tf->tf_trapno == T_PGFLT)
		cprintf("  cr2  0x%08x\n", rcr2());
	cprintf("  err  0x%08x", tf->tf_err);
	// For page faults, print decoded fault error code:
	// U/K=fault occurred in user/kernel mode
	// W/R=a write/read caused the fault
	// PR=a protection violation caused the fault (NP=page not present).
	if (tf->tf_trapno == T_PGFLT)
		cprintf(" [%s, %s, %s]\n",
			tf->tf_err & 4 ? "user" : "kernel",
			tf->tf_err & 2 ? "write" : "read",
			tf->tf_err & 1 ? "protection" : "not-present");
	else
		cprintf("\n");
	cprintf("  rip  0x%08x\n", tf->tf_rip);
	cprintf("  cs   0x----%04x\n", tf->tf_cs);
	cprintf("  flag 0x%08x\n", tf->tf_eflags);
	if ((tf->tf_cs & 3) != 0) {
		cprintf("  rsp  0x%08x\n", tf->tf_rsp);
		cprintf("  ss   0x----%04x\n", tf->tf_ss);
	}
}

void
print_regs(struct PushRegs *regs)
{
	cprintf("  r15  0x%08x\n", regs->reg_r15);
	cprintf("  r14  0x%08x\n", regs->reg_r14);
	cprintf("  r13  0x%08x\n", regs->reg_r13);
	cprintf("  r12  0x%08x\n", regs->reg_r12);
	cprintf("  r11  0x%08x\n", regs->reg_r11);
	cprintf("  r10  0x%08x\n", regs->reg_r10);
	cprintf("  r9  0x%08x\n", regs->reg_r9);
	cprintf("  r8  0x%08x\n", regs->reg_r8);
	cprintf("  rdi  0x%08x\n", regs->reg_rdi);
	cprintf("  rsi  0x%08x\n", regs->reg_rsi);
	cprintf("  rbp  0x%08x\n", regs->reg_rbp);
	cprintf("  rbx  0x%08x\n", regs->reg_rbx);
	cprintf("  rdx  0x%08x\n", regs->reg_rdx);
	cprintf("  rcx  0x%08x\n", regs->reg_rcx);
	cprintf("  rax  0x%08x\n", regs->reg_rax);
}

static void
trap_dispatch(struct Trapframe *tf)
{
	// Handle processor exceptions.
	// LAB 3: Your code here.
	int64_t i;
	if(tf->tf_trapno == T_PGFLT){
			//print_trapframe(tf);
			page_fault_handler(tf);
			return;
	}
	else if(tf->tf_trapno == T_BRKPT) {
			monitor(tf);
			return;
	}
	else if(tf->tf_trapno == T_SYSCALL) {
			i = syscall(tf->tf_regs.reg_rax, tf->tf_regs.reg_rdx, tf->tf_regs.reg_rcx, tf->tf_regs.reg_rbx, tf->tf_regs.reg_rdi, tf->tf_regs.reg_rsi);
			tf->tf_regs.reg_rax = i;
			return;
	}
	
	// Handle spurious interrupts
	// The hardware sometimes raises these because of noise on the
	// IRQ line or other reasons. We don't care.
	if (tf->tf_trapno == IRQ_OFFSET + IRQ_SPURIOUS) {
		cprintf("Spurious interrupt on irq 7\n");
		print_trapframe(tf);
		return;
	}

	// Handle clock interrupts. Don't forget to acknowledge the
	// interrupt using lapic_eoi() before calling the scheduler!
	// LAB 4: Your code here.

	// Add time tick increment to clock interrupts.
	// Be careful! In multiprocessors, clock interrupts are
	// triggered on every CPU.
	// LAB 6: Your code here.

	if(tf->tf_trapno == T_IRQ0) {
		lapic_eoi();
	
		// Add time tick increment to clock interrupts.
		// Be careful! In multiprocessors, clock interrupts are
		// triggered on every CPU. 								WHY HAS HE LEFT THIS CRYPTIC COMMENT? WHEN IT TRAPS WE ALREADY HAVE LOCK.
		// LAB 6: Your code here.
		time_tick();
		
		sched_yield();
		return;
	}
	
	// Handle keyboard and serial interrupts.
	// LAB 7: Your code here.
	if (tf->tf_trapno == T_IRQ1) {
		kbd_intr();
		return;
	}
	if(tf->tf_trapno == T_IRQ4) {
		serial_intr();
		return;
	}
	
	// Unexpected trap: The user process or the kernel has a bug.
	print_trapframe(tf);
	if (tf->tf_cs == GD_KT)
		panic("unhandled trap in kernel");
	else {
		env_destroy(curenv);
		return;
	}
}

void
trap(struct Trapframe *tf)
{
	//cprintf("entered trap\n");
    //struct Trapframe *tf = &tf_;
	// The environment may have set DF and some versions
	// of GCC rely on DF being clear
	asm volatile("cld" ::: "cc");

	// Halt the CPU if some other CPU has called panic()
	extern char *panicstr;
	if (panicstr)
		asm volatile("hlt");

	// Check that interrupts are disabled.  If this assertion
	// fails, DO NOT be tempted to fix it by inserting a "cli" in
	// the interrupt path.
	assert(!(read_eflags() & FL_IF));

	//cprintf("\nIncoming TRAP frame at %x\n", tf);
	//print_trapframe(tf);

	if ((tf->tf_cs & 3) == 3) {
		// Trapped from user mode.
		// Acquire the big kernel lock before doing any
		// serious kernel work.
		// LAB 4: Your code here.
//		lock_kernel();
		assert(curenv);
		
		// Garbage collect if current enviroment is a zombie
		if (curenv->env_status == ENV_DYING) {
			env_free(curenv);
			curenv = NULL;
			sched_yield();
		}

		// Copy trap frame (which is currently on the stack)
		// into 'curenv->env_tf', so that running the environment
		// will restart at the trap point.
		curenv->env_tf = *tf;
		// The trapframe on the stack should be ignored from here on.
		tf = &curenv->env_tf;
	}

	// Record that tf is the last real trapframe so
	// print_trapframe can print some additional information.
	last_tf = tf;
	
	//cprintf("entering trap_dispatch\n");
	// Dispatch based on what type of trap occurred
	trap_dispatch(tf);
	//cprintf("entering trap_dispatch2\n");

	
	// If we made it to this point, then no other environment was
	// scheduled, so we should return to the current environment
	// if doing so makes sense.
	if (curenv && curenv->env_status == ENV_RUNNING)
		env_run(curenv);
	else
		sched_yield();
		
/*	// Return to the current environment, which should be running.
	assert(curenv && curenv->env_status == ENV_RUNNING);
	//cprintf("entering trap_dispatch3\n");
	env_run(curenv);*/
}


void
page_fault_handler(struct Trapframe *tf)
{
	uint64_t fault_va;

	// Read processor's CR2 register to find the faulting address
	fault_va = rcr2();

	// Handle kernel-mode page faults.
	// LAB 3: Your code here.
	//cprintf("PGFault at %x\n",fault_va);
	if (tf->tf_cs == 0x8) {
		print_trapframe(tf);
		panic("Kernel Pagefaulted!");
	}

	// We've already handled kernel-mode exceptions, so if we get here,
	// the page fault happened in user mode.

	// Call the environment's page fault upcall, if one exists.  Set up a
	// page fault stack frame on the user exception stack (below
	// UXSTACKTOP), then branch to curenv->env_pgfault_upcall.
	//
	// The page fault upcall might cause another page fault, in which case
	// we branch to the page fault upcall recursively, pushing another
	// page fault stack frame on top of the user exception stack.
	//
	// The trap handler needs one word of scratch space at the top of the
	// trap-time stack in order to return.  In the non-recursive case, we
	// don't have to worry about this because the top of the regular user
	// stack is free.  In the recursive case, this means we have to leave
	// an extra word between the current top of the exception stack and
	// the new stack frame because the exception stack _is_ the trap-time
	// stack.
	//
	//
	// If there's no page fault upcall, the environment didn't allocate a
	// page for its exception stack or can't write to it, or the exception
	// stack overflows, then destroy the environment that caused the fault.
	// Note that the grade script assumes you will first check for the page
	// fault upcall and print the "user fault va" message below if there is
	// none.  The remaining three checks can be combined into a single test.
	//
	/*
	uint64_t utf_fault_va;	// va for T_PGFLT, 0 otherwise
	uint64_t utf_err;
	//trap-time return state 
	struct PushRegs utf_regs;
	uintptr_t utf_rip;
	uint64_t utf_eflags;
	//the trap-time stack to return to
	uintptr_t utf_rsp;
	*/
	// Hints:
	//   user_mem_assert() and env_run() are useful here.
	//void user_mem_assert(struct Env *env, const void *va, size_t len, int perm)
	//void env_run(struct Env *e)
	//   To change what the user environment runs, modify 'curenv->env_tf'
	//   (the 'tf' variable points at 'curenv->env_tf').

	// LAB 4: Your code here.
	if(curenv->env_pgfault_upcall) {
		uintptr_t stacktop = UXSTACKTOP;
		//print_trapframe(tf);
		//cprintf("%x\n",curenv->env_id);
		if (tf->tf_rsp < UXSTACKTOP && tf->tf_rsp >= UXSTACKTOP-PGSIZE)
			stacktop = tf->tf_rsp - sizeof(uintptr_t);
		stacktop -= sizeof(struct UTrapframe);
		struct UTrapframe *u = (struct UTrapframe*) stacktop;
		user_mem_assert(curenv, u, sizeof(struct UTrapframe), PTE_W);
		u->utf_fault_va = fault_va;
		u->utf_err = tf->tf_err;
		u->utf_regs = tf->tf_regs;
		u->utf_rip = tf->tf_rip;
		u->utf_eflags = tf->tf_eflags;
		u->utf_rsp = tf->tf_rsp;

		tf->tf_rsp = stacktop;
		tf->tf_rip = (uintptr_t)curenv->env_pgfault_upcall;
		//cprintf("handled\n");
		env_run(curenv);
        }

	// Destroy the environment that caused the fault.
	cprintf("[%08x] user fault va %08x ip %08x\n",
		curenv->env_id, fault_va, tf->tf_rip);
	print_trapframe(tf);
	env_destroy(curenv);
}

