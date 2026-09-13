#pragma once
#include <stdint.h>
#include <stddef.h>

#define PAGE_SIZE 4096

void   pmm_init(uint32_t mb2_magic, uint64_t mb2_info);
void  *pmm_alloc(void);
void   pmm_free(void *page);
/* Contiguous multi-page allocations (first-fit). Large kmallocs must be
 * physically linear because callers treat them as one buffer. */
void  *pmm_alloc_contig(size_t count);
void   pmm_free_contig(void *base, size_t count);
size_t pmm_free_pages(void);
size_t pmm_total_pages(void);
