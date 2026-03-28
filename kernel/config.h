#ifndef _CONFIG_H_
#define _CONFIG_H_

// we use two HART (cpu) in challenge3
#define NCPU 2

//interval of timer interrupt. added @lab1_3
#define TIMER_INTERVAL 1000000

#define DRAM_BASE 0x80000000

/*
 * Bare mode in lab1 maps VA directly to PA.
 * Reserve a 64MB user region for each hart to avoid overlap across harts.
 */
#define USER_SPACE_SIZE 0x04000000UL
#define USER_SPACE_BASE 0x81000000UL

#define USER_SPACE_BASE_FOR_HART(hartid) (USER_SPACE_BASE + (hartid) * USER_SPACE_SIZE)

// user stack top
#define USER_STACK_FOR_HART(hartid) (USER_SPACE_BASE_FOR_HART(hartid) + 0x00100000UL)

// the stack used by PKE kernel when a syscall happens
#define USER_KSTACK_FOR_HART(hartid) (USER_SPACE_BASE_FOR_HART(hartid) + 0x00200000UL)

// the trap frame used to assemble the user "process"
#define USER_TRAP_FRAME_FOR_HART(hartid) (USER_SPACE_BASE_FOR_HART(hartid) + 0x00300000UL)

#endif
