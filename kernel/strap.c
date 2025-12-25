/*
 * Utility functions for trap handling in Supervisor mode.
 */

#include "riscv.h"
#include "process.h"
#include "strap.h"
#include "syscall.h"
#include "pmm.h"
#include "vmm.h"
#include "memlayout.h"
#include "util/functions.h"

#include "spike_interface/spike_utils.h"

//
// handling the syscalls. will call do_syscall() defined in kernel/syscall.c
//
static void handle_syscall(trapframe *tf) {
  // tf->epc points to the address that our computer will jump to after the trap handling.
  // for a syscall, we should return to the NEXT instruction after its handling.
  // in RV64G, each instruction occupies exactly 32 bits (i.e., 4 Bytes)
  tf->epc += 4;

  // TODO (lab1_1): remove the panic call below, and call do_syscall (defined in
  // kernel/syscall.c) to conduct real operations of the kernel side for a syscall.
  // IMPORTANT: return value should be returned to user app, or else, you will encounter
  // problems in later experiments!
  tf->regs.a0 = do_syscall(tf->regs.a0, tf->regs.a1, tf->regs.a2, tf->regs.a3,
                           tf->regs.a4, tf->regs.a5, tf->regs.a6, tf->regs.a7);
}

//
// global variable that store the recorded "ticks". added @lab1_3
static uint64 g_ticks = 0;
//
// added @lab1_3
//
void handle_mtimer_trap() {
  sprint("Ticks %d\n", g_ticks);
  // TODO (lab1_3): increase g_ticks to record this "tick", and then clear the "SIP"
  // field in sip register.
  // hint: use write_csr to disable the SIP_SSIP bit in sip.
  g_ticks++;
  uint64 sip = read_csr(sip);
  sip &= ~SIP_SSIP;
  write_csr(sip, sip);
}

//
// the page fault handler. added @lab2_3. parameters:
// sepc: the pc when fault happens;
// stval: the virtual address that causes pagefault when being accessed.
//
void handle_user_page_fault(uint64 mcause, uint64 sepc, uint64 stval) {
  sprint("handle_page_fault: %lx\n", stval);

  // Check if the fault address is valid for stack growth.
  // Valid stack growth: address is in the stack region, which is:
  // - Less than USER_STACK_TOP (0x7ffff000)
  // - Greater than or equal to a minimum stack address (we use 0x70000000 as boundary)
  // This prevents illegal access to unmapped regions like heap out-of-bounds.
  if (stval < USER_STACK_TOP && stval >= USER_STACK_TOP - (1 << 23)) {
    // Address is in stack region, check if page table entry exists.
    // Use page_walk to check if the page table entry exi sts for this virtual address.
    pte_t *pte = page_walk(current->pagetable, stval, 0);

    // Allocate a new page if the PTE doesn't exist or is invalid.
    if (pte == 0 || *pte == 0) {
      switch (mcause) {
        case CAUSE_STORE_PAGE_FAULT:
        case CAUSE_LOAD_PAGE_FAULT:
          {
            uint64 pa = (uint64)alloc_page();
            if (pa == 0) {
              panic("handle_user_page_fault: alloc_page failed!");
            }
            uint64 va = ROUNDDOWN(stval, PGSIZE);
            if (map_pages(current->pagetable, va, PGSIZE, pa,
                          prot_to_type(PROT_READ | PROT_WRITE, 1)) != 0) {
              panic("handle_user_page_fault: map_pages failed!");
            }
          }
          break;
        default:
          sprint("unknown page fault.\n");
          break;
      }
    } else {
      // PTE exists and is valid, but still got page fault - invalid access.
      sprint("this address is not available!\n");
      shutdown(-1);
    }
  } else {
    // Address is not in stack region - invalid access.
    sprint("this address is not available!\n");
    shutdown(-1);
  }
}

//
// kernel/smode_trap.S will pass control to smode_trap_handler, when a trap happens
// in S-mode.
//
void smode_trap_handler(void) {
  // make sure we are in User mode before entering the trap handling.
  // we will consider other previous case in lab1_3 (interrupt).
  if ((read_csr(sstatus) & SSTATUS_SPP) != 0) panic("usertrap: not from user mode");

  assert(current);
  // save user process counter.
  current->trapframe->epc = read_csr(sepc);

  // if the cause of trap is syscall from user application.
  // read_csr() and CAUSE_USER_ECALL are macros defined in kernel/riscv.h
  uint64 cause = read_csr(scause);

  // use switch-case instead of if-else, as there are many cases since lab2_3.
  switch (cause) {
    case CAUSE_USER_ECALL:
      handle_syscall(current->trapframe);
      break;
    case CAUSE_MTIMER_S_TRAP:
      handle_mtimer_trap();
      break;
    case CAUSE_STORE_PAGE_FAULT:
    case CAUSE_LOAD_PAGE_FAULT:
      // the address of missing page is stored in stval
      // call handle_user_page_fault to process page faults
      handle_user_page_fault(cause, read_csr(sepc), read_csr(stval));
      break;
    default:
      sprint("smode_trap_handler(): unexpected scause %p\n", read_csr(scause));
      sprint("            sepc=%p stval=%p\n", read_csr(sepc), read_csr(stval));
      panic( "unexpected exception happened.\n" );
      break;
  }

  // continue (come back to) the execution of current process.
  switch_to(current);
}
