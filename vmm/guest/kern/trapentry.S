/* See COPYRIGHT for copyright information. */

#include <inc/mmu.h>
#include <inc/memlayout.h>
#include <inc/trap.h>
#include <kern/macro.h>

#include <kern/picirq.h>


###################################################################
# exceptions/interrupts
###################################################################

/* TRAPHANDLER defines a globally-visible function for handling a trap.
 * It pushes a trap number onto the stack, then jumps to _alltraps.
 * Use TRAPHANDLER for traps where the CPU automatically pushes an error code.
 *
 * You shouldn't call a TRAPHANDLER function from C, but you may
 * need to _declare_ one in C (for instance, to get a function pointer
 * during IDT setup).  You can declare the function with
 *   void NAME();
 * where NAME is the argument passed to TRAPHANDLER.
 */
#define TRAPHANDLER(name, num)						\
	.globl name;		/* define global symbol for 'name' */	\
	.type name, @function;	/* symbol type is function */		\
	.align 2;		/* align function definition */		\
	name:			/* function starts here */		\
	pushq $(num);							\
	jmp _alltraps

/* Use TRAPHANDLER_NOEC for traps where the CPU doesn't push an error code.
 * It pushes a 0 in place of the error code, so the trap frame has the same
 * format in either case.
 */
#define TRAPHANDLER_NOEC(name, num)					\
	.globl name;							\
	.type name, @function;						\
	.align 2;							\
	name:								\
	pushq $0;							\
	pushq $(num);							\
	jmp _alltraps

.text

/*
 * Lab 3: Your code here for generating entry points for the different traps.
   Find the PUSHA,POPA macros in kern/macro.h. Since amd64 doesn't support
   pusha,popa so you will find these macros handy.
 */
TRAPHANDLER_NOEC(divide_error_handler, T_DIVIDE)
TRAPHANDLER_NOEC(debug_exception_handler, T_DEBUG)
TRAPHANDLER_NOEC(non_maskable_interrupt_handler, T_NMI)
TRAPHANDLER_NOEC(breakpoint_handler, T_BRKPT)
TRAPHANDLER_NOEC(overflow_handler, T_OFLOW)
TRAPHANDLER_NOEC(bounds_check_handler, T_BOUND)
TRAPHANDLER_NOEC(illegal_opcode_handler, T_ILLOP)
TRAPHANDLER_NOEC(device_no_available_handler, T_DEVICE)
TRAPHANDLER(double_fault_handler, T_DBLFLT)
#TRAPHANDLER_NOEC(reserved_9_handler, T_COPROC)
TRAPHANDLER(invalid_tss_handler, T_TSS)
TRAPHANDLER(segment_not_present_handler, T_SEGNP)
TRAPHANDLER(stack_exception_handler, T_STACK)
TRAPHANDLER(general_protection_fault_handler, T_GPFLT)
TRAPHANDLER(page_fault_handler_1, T_PGFLT)
#TRAPHANDLER(reserved_15_handler, T_RES)
TRAPHANDLER_NOEC(floating_point_error_handler, T_FPERR)
TRAPHANDLER_NOEC(allign_check_handler, T_ALIGN)
TRAPHANDLER_NOEC(machine_check_handler, T_MCHK)
TRAPHANDLER_NOEC(SIMD_floating_point_error_handler, T_SIMDERR)
TRAPHANDLER_NOEC(system_call_handler, T_SYSCALL)
#TRAPHANDLER(catchall_handler, T_DEFAULT)

//IRQs
TRAPHANDLER_NOEC(irq_timer_handler, IRQ_TIMER+IRQ_OFFSET)
TRAPHANDLER_NOEC(irq_kbd_handler, IRQ_KBD+IRQ_OFFSET)
TRAPHANDLER_NOEC(irq_serial_handler, IRQ_SERIAL+IRQ_OFFSET)
TRAPHANDLER_NOEC(irq_spurious_handler, IRQ_SPURIOUS+IRQ_OFFSET)
TRAPHANDLER_NOEC(irq_ide_handler, IRQ_IDE+IRQ_OFFSET)


/*
 * Lab 3: Your code here for _alltraps
 *
 * Hint: Be sure to review the x64 calling convention from lab1
 *       for how to pass a pointer to the trapframe.
 */

_alltraps:
	movw %ds, %r10;
	pushq %r10;
	movw %es, %r10;
	pushq %r10;
	PUSHA;
	movw $GD_KD, %r10;
	movw %r10, %ds
	movw %r10, %es
	movq %rsp,%rdi
	call trap;
	#TODO: POP? iretq?
