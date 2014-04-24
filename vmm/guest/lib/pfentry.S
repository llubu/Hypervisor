#include <inc/mmu.h>
#include <inc/memlayout.h>
#include <kern/macro.h>

// Page fault upcall entrypoint.

// This is where we ask the kernel to redirect us to whenever we cause
// a page fault in user space (see the call to sys_set_pgfault_handler
// in pgfault.c).
//
// When a page fault actually occurs, the kernel switches our ESP to
// point to the user exception stack if we're not already on the user
// exception stack, and then it pushes a UTrapframe onto our user
// exception stack:
//
//	<?Blank word?>
//	uintptr_t utf_rsp;
//	uint64_t utf_eflags;
//	uintptr_t utf_rip;
//	struct PushRegs utf_regs;
//	uint64_t utf_err;
//	uint64_t utf_fault_va;            <-- %esp
// If this is a recursive fault, the kernel will reserve for us a
// blank word above the trap-time esp for scratch work when we unwind
// the recursive call.
//
// We then have to call up to the appropriate page fault handler in C
// code, pointed to by the global variable '_pgfault_handler'.

.text
.globl _pgfault_upcall
_pgfault_upcall:
	// Call the C page fault handler.
	//pushq %rsp			// function argument: pointer to UTF
	
	movq  %rsp,%rdi
	//addq  $8,%rdi      // passing the function argument in rdi
	movabs _pgfault_handler, %rax
	call *%rax
	
	// Now the C page fault handler has returned and you must return
	// to the trap time state.
	// Push trap-time %eip onto the trap-time stack.
	//
	// Explanation:
	//   We must prepare the trap-time stack for our eventual return to
	//   re-execute the instruction that faulted.
	//   Unfortunately, we can't return directly from the exception stack:
	//   We can't call 'jmp', since that requires that we load the address
	//   into a register, and all registers must have their trap-time
	//   values after the return.
	//   We can't call 'ret' from the exception stack either, since if we
	//   did, %esp would have the wrong value.
	//   So instead, we push the trap-time %eip onto the *trap-time* stack!
	//   Below we'll switch to that stack and call 'ret', which will
	//   restore %eip to its pre-fault value.
	//
	//   In the case of a recursive fault on the exception stack,
	//   note that the word we're pushing now will fit in the
	//   blank word that the kernel reserved for us.
	//
	// Throughout the remaining code, think carefully about what
	// registers are available for intermediate calculations.  You
	// may find that you have to rearrange your code in non-obvious
	// ways as registers become unavailable as scratch space.
	//
	//	<?Blank word?>
	//	uintptr_t utf_rsp;				<-- %esp78 %rsp95
	//	uint64_t utf_eflags;			<-- %rsp94
	//	uintptr_t utf_rip;				<-- %esp76 %rsp88
	//	struct PushRegs utf_regs;		<-- %esp75 %rsp83
	//	uint64_t utf_err;
	//	uint64_t utf_fault_va;			<-- %esp
	// LAB 4: Your code here.
	//popq %rsp
	movq 136(%rsp), %rdx				//tt_rip
	movq 152(%rsp), %rax				//tt_rsp
	subq $8, %rax						//subtract one word to write there
	movq %rdx, (%rax)					//write tt_rip to tt_rsp-8
	movq %rax, 152(%rsp)				// write the updated tt_rsp value to tt_rsp on the stack.
	addq $16, %rsp
		
	// Restore the trap-time registers.  After you do this, you
	// can no longer modify any general-purpose registers.
	// LAB 4: Your code here.
	POPA_
	
	// Restore eflags from the stack.  After you do this, you can
	// no longer use arithmetic operations or anything else that
	// modifies eflags.
	// LAB 4: Your code here.
	addq $8, %rsp
	popfq

	// Switch back to the adjusted trap-time stack.
	// LAB 4: Your code here.
	popq %rsp

	// Return to re-execute the instruction that faulted.
	// LAB 4: Your code here.
	ret
