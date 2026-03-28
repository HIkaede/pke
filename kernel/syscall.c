/*
 * contains the implementation of all syscalls.
 */

#include <stdint.h>
#include <errno.h>

#include "util/types.h"
#include "syscall.h"
#include "string.h"
#include "process.h"
#include "util/functions.h"
#include "pmm.h"
#include "vmm.h"
#include "memlayout.h"
#include "spike_interface/spike_utils.h"

typedef struct heap_block_meta_t {
  uint64 va;
  uint64 size;
  int free;
  int used;
} heap_block_meta;

#define HEAP_BLOCK_ALIGN 16UL
#define HEAP_MAX_BLOCKS 128

static heap_block_meta g_heap_blocks[HEAP_MAX_BLOCKS];
static int g_heap_initialized = 0;

static void coalesce_free_blocks(void);

static inline uint64 align_up(uint64 val, uint64 align) {
  return (val + align - 1) & ~(align - 1);
}

static int alloc_block_slot(void) {
  for (int i = 0; i < HEAP_MAX_BLOCKS; i++) {
    if (!g_heap_blocks[i].used) {
      g_heap_blocks[i].used = 1;
      return i;
    }
  }
  return -1;
}

static int map_new_heap_page(void) {
  uint64 page_va = current->ufree_page;
  void* pa = alloc_page();
  if (!pa) return -1;

  user_vm_map((pagetable_t)current->pagetable, page_va, PGSIZE, (uint64)pa,
              prot_to_type(PROT_WRITE | PROT_READ, 1));
  current->ufree_page += PGSIZE;

  int slot = alloc_block_slot();
  if (slot < 0) return -1;
  g_heap_blocks[slot].va = page_va;
  g_heap_blocks[slot].size = PGSIZE;
  g_heap_blocks[slot].free = 1;
  return slot;
}

static int map_heap_pages(uint64 start_va, uint64 size) {
  uint64 first = ROUNDDOWN(start_va, PGSIZE);
  uint64 last = ROUNDDOWN(start_va + size - 1, PGSIZE);
  for (uint64 va = first; va <= last; va += PGSIZE) {
    if (user_va_to_pa((pagetable_t)current->pagetable, (void*)va)) continue;
    void* pa = alloc_page();
    if (!pa) return -1;
    user_vm_map((pagetable_t)current->pagetable, va, PGSIZE, (uint64)pa,
                prot_to_type(PROT_WRITE | PROT_READ, 1));
  }
  return 0;
}

static void maybe_init_heap(void) {
  if (g_heap_initialized) return;
  for (int i = 0; i < HEAP_MAX_BLOCKS; i++) {
    g_heap_blocks[i].used = 0;
    g_heap_blocks[i].free = 0;
    g_heap_blocks[i].va = 0;
    g_heap_blocks[i].size = 0;
  }
  if (current->ufree_page < USER_FREE_ADDRESS_START)
    current->ufree_page = USER_FREE_ADDRESS_START;

  if (map_new_heap_page() < 0)
    panic("sys_user_allocate_page: failed to initialize heap page");
  g_heap_initialized = 1;
}

static int find_best_fit(uint64 req_size) {
  int best = -1;
  for (int i = 0; i < HEAP_MAX_BLOCKS; i++) {
    if (!g_heap_blocks[i].used || !g_heap_blocks[i].free) continue;
    if (g_heap_blocks[i].size < req_size) continue;
    if (best < 0 || g_heap_blocks[i].va < g_heap_blocks[best].va) best = i;
  }
  return best;
}

static int ensure_heap_room(uint64 req_size) {
  while (1) {
    coalesce_free_blocks();
    int idx = find_best_fit(req_size);
    if (idx >= 0) return idx;
    if (map_new_heap_page() < 0) return -1;
  }
}

static void split_block_if_needed(int idx, uint64 req_size) {
  if (g_heap_blocks[idx].size <= req_size + HEAP_BLOCK_ALIGN) return;

  int tail = alloc_block_slot();
  if (tail < 0) return;
  g_heap_blocks[tail].va = g_heap_blocks[idx].va + req_size;
  g_heap_blocks[tail].size = g_heap_blocks[idx].size - req_size;
  g_heap_blocks[tail].free = 1;

  g_heap_blocks[idx].size = req_size;
}

static void coalesce_free_blocks(void) {
  int merged = 1;
  while (merged) {
    merged = 0;
    for (int i = 0; i < HEAP_MAX_BLOCKS && !merged; i++) {
      if (!g_heap_blocks[i].used || !g_heap_blocks[i].free) continue;
      for (int j = 0; j < HEAP_MAX_BLOCKS; j++) {
        if (i == j || !g_heap_blocks[j].used || !g_heap_blocks[j].free) continue;
        if (g_heap_blocks[i].va + g_heap_blocks[i].size == g_heap_blocks[j].va) {
          g_heap_blocks[i].size += g_heap_blocks[j].size;
          g_heap_blocks[j].used = 0;
          merged = 1;
          break;
        }
        if (g_heap_blocks[j].va + g_heap_blocks[j].size == g_heap_blocks[i].va) {
          g_heap_blocks[j].size += g_heap_blocks[i].size;
          g_heap_blocks[i].used = 0;
          merged = 1;
          break;
        }
      }
    }
  }
}

//
// implement the SYS_user_print syscall
//
ssize_t sys_user_print(const char* buf, size_t n) {
  // buf is now an address in user space of the given app's user stack,
  // so we have to transfer it into phisical address (kernel is running in direct mapping).
  assert( current );
  char* pa = (char*)user_va_to_pa((pagetable_t)(current->pagetable), (void*)buf);
  sprint(pa);
  return 0;
}

//
// implement the SYS_user_exit syscall
//
ssize_t sys_user_exit(uint64 code) {
  sprint("User exit with code:%d.\n", code);
  // in lab1, PKE considers only one app (one process). 
  // therefore, shutdown the system when the app calls exit()
  shutdown(code);
}

//
// challenge2: a compact allocator that sub-allocates blocks inside heap pages.
uint64 sys_user_allocate_page(uint64 n) {
  assert(current);
  if ((int64)n <= 0) return 0;

  uint64 req_size = align_up(n, HEAP_BLOCK_ALIGN);

  maybe_init_heap();

  int idx = ensure_heap_room(req_size);
  if (idx < 0) return 0;

  split_block_if_needed(idx, req_size);
  g_heap_blocks[idx].free = 0;

  if (map_heap_pages(g_heap_blocks[idx].va, g_heap_blocks[idx].size) < 0) {
    g_heap_blocks[idx].free = 1;
    return 0;
  }

  return g_heap_blocks[idx].va;
}

//
// reclaim a page, indicated by "va". added @lab2_2
//
uint64 sys_user_free_page(uint64 va) {
  if (va == 0) return 0;

  for (int i = 0; i < HEAP_MAX_BLOCKS; i++) {
    if (!g_heap_blocks[i].used) continue;
    if (g_heap_blocks[i].va != va) continue;
    if (g_heap_blocks[i].free) return 0;
    g_heap_blocks[i].free = 1;
    coalesce_free_blocks();
    return 0;
  }

  coalesce_free_blocks();
  return 0;
}

//
// [a0]: the syscall number; [a1] ... [a7]: arguments to the syscalls.
// returns the code of success, (e.g., 0 means success, fail for otherwise)
//
long do_syscall(long a0, long a1, long a2, long a3, long a4, long a5, long a6, long a7) {
  switch (a0) {
    case SYS_user_print:
      return sys_user_print((const char*)a1, a2);
    case SYS_user_exit:
      return sys_user_exit(a1);
    // added @lab2_2
    case SYS_user_allocate_page:
      return sys_user_allocate_page(a1);
    case SYS_user_free_page:
      return sys_user_free_page(a1);
    default:
      panic("Unknown syscall %ld \n", a0);
  }
}
