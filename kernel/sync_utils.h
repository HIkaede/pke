#ifndef _SYNC_UTILS_H_
#define _SYNC_UTILS_H_

typedef struct {
  volatile int locked;
} spinlock_t;

static inline void spinlock_init(spinlock_t *lock) {
  lock->locked = 0;
}

static inline void spinlock_acquire(spinlock_t *lock) {
  int old;
  do {
    asm volatile("amoswap.w.aq %0, %2, (%1)\n"
                 : "=r"(old)
                 : "r"(&lock->locked), "r"(1)
                 : "memory");
  } while (old != 0);
}

static inline void spinlock_release(spinlock_t *lock) {
  asm volatile("amoswap.w.rl x0, x0, (%0)\n" : : "r"(&lock->locked) : "memory");
}

static inline void sync_barrier(volatile int *counter, int all) {

  int local;

  asm volatile("amoadd.w %0, %2, (%1)\n"
               : "=r"(local)
               : "r"(counter), "r"(1)
               : "memory");

  if (local + 1 < all) {
    do {
      asm volatile("lw %0, (%1)\n" : "=r"(local) : "r"(counter) : "memory");
    } while (local < all);
  }
}

#endif