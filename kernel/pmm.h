#ifndef _PMM_H_
#define _PMM_H_

// Initialize phisical memeory manager
void pmm_init();
// Allocate a free phisical page
void* alloc_page();
// Free an allocated page
void free_page(void* pa);
// Increase ref count of an allocated page.
void page_ref_inc(void* pa);
// Get current ref count of an allocated page.
int page_ref_get(void* pa);

#endif