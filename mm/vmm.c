#include "vmm.h"
#include "pmm.h"
#include "slab.h"
#include "../lib/string.h"

static uint64_t *kernel_pml4 = NULL;
static bool slab_ready = false;

static uint64_t *
get_or_alloc(uint64_t *table, int idx, uint64_t flags)
{
    if (!(table[idx] & VMM_PRESENT)) {
        void *page = pmm_alloc();
        if (!page) return NULL;
        kmemset(page, 0, PAGE_SIZE);
        table[idx] = (uint64_t)page | flags;
        return (uint64_t *)page;
    }
    if (table[idx] & VMM_HUGE) {
        /* Split a 2 MiB huge-page mapping (the boot identity map covers
         * the low 1 GiB this way) into a fresh page table so a smaller
         * mapping can be installed at this level. The 2 MiB frame stays
         * mapped, now as 512 4 KiB entries, in this address space only.
         * The new table link takes the caller's flags: a user mapping
         * needs U/S set on EVERY level of the walk, not just the leaf. */
        uint64_t entry = table[idx];
        uint64_t *pt = pmm_alloc();
        if (!pt) return NULL;
        uint64_t base = entry & ~0x1FFFFFULL;
        uint64_t pte_flags = entry & (VMM_PRESENT | VMM_WRITABLE);
        for (int i = 0; i < 512; i++) {
            pt[i] = (base + ((uint64_t)i << 12)) | pte_flags;
        }
        table[idx] = ((uint64_t)pt & ~0xFFFULL) | (flags & 0xFFF);
        return pt;
    }
    /* Same rule for pre-existing links: installing a user mapping under
     * a supervisor-only link would fault at CPL3 despite the leaf flags. */
    if ((flags & VMM_USER) && !(table[idx] & VMM_USER)) {
        table[idx] |= VMM_USER;
    }
    return (uint64_t *)(table[idx] & ~0xFFFULL);
}

void
vmm_map(uint64_t *pml4, uint64_t virt, uint64_t phys, uint64_t flags)
{
    int p4 = (virt >> 39) & 0x1FF;
    int p3 = (virt >> 30) & 0x1FF;
    int p2 = (virt >> 21) & 0x1FF;
    int p1 = (virt >> 12) & 0x1FF;

    uint64_t tbl_flags = VMM_PRESENT | VMM_WRITABLE;
    if (flags & VMM_USER) tbl_flags |= VMM_USER;

    uint64_t *pdpt = get_or_alloc(pml4, p4, tbl_flags);
    uint64_t *pdt  = get_or_alloc(pdpt, p3, tbl_flags);
    uint64_t *pt   = get_or_alloc(pdt,  p2, tbl_flags);
    pt[p1] = (phys & ~0xFFFULL) | (flags & 0xFFF) | VMM_PRESENT;
    if (flags & VMM_NX) pt[p1] |= VMM_NX;

    __asm__ volatile ("invlpg (%0)" :: "r"(virt) : "memory");
}

void
vmm_unmap(uint64_t *pml4, uint64_t virt)
{
    int p4 = (virt >> 39) & 0x1FF;
    int p3 = (virt >> 30) & 0x1FF;
    int p2 = (virt >> 21) & 0x1FF;
    int p1 = (virt >> 12) & 0x1FF;

    if (!(pml4[p4] & VMM_PRESENT)) return;
    uint64_t *pdpt = (uint64_t *)(pml4[p4] & ~0xFFFULL);
    if (!(pdpt[p3] & VMM_PRESENT)) return;
    uint64_t *pdt  = (uint64_t *)(pdpt[p3] & ~0xFFFULL);
    if (!(pdt[p2]  & VMM_PRESENT)) return;
    uint64_t *pt   = (uint64_t *)(pdt[p2]  & ~0xFFFULL);
    pt[p1] = 0;
    __asm__ volatile ("invlpg (%0)" :: "r"(virt) : "memory");
}

uint64_t
vmm_get_phys(uint64_t *pml4, uint64_t virt)
{
    int p4 = (virt >> 39) & 0x1FF;
    int p3 = (virt >> 30) & 0x1FF;
    int p2 = (virt >> 21) & 0x1FF;
    int p1 = (virt >> 12) & 0x1FF;

    if (!(pml4[p4] & VMM_PRESENT)) return 0;
    uint64_t *pdpt = (uint64_t *)(pml4[p4] & ~0xFFFULL);
    if (!(pdpt[p3] & VMM_PRESENT)) return 0;
    uint64_t *pdt  = (uint64_t *)(pdpt[p3] & ~0xFFFULL);
    if (!(pdt[p2]  & VMM_PRESENT)) return 0;
    if (pdt[p2] & VMM_HUGE)
        return (pdt[p2] & ~0x1FFFFFULL) + (virt & 0x1FFFFFULL);
    uint64_t *pt   = (uint64_t *)(pdt[p2]  & ~0xFFFULL);
    return pt[p1] & ~0xFFFULL;
}

void
vmm_map_range(uint64_t *pml4, uint64_t virt, uint64_t phys,
              size_t size, uint64_t flags)
{
    for (size_t off = 0; off < size; off += PAGE_SIZE)
        vmm_map(pml4, virt + off, phys + off, flags);
}

uint64_t *
vmm_kernel_pml4(void) { return kernel_pml4; }

uint64_t *
vmm_current(void)
{
    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    return (uint64_t *)(cr3 & ~0xFFFULL);
}

void
vmm_switch(uint64_t *pml4)
{
    __asm__ volatile ("mov %0, %%cr3" :: "r"((uint64_t)pml4) : "memory");
}

uint64_t *
vmm_new_pml4(void)
{
    uint64_t *pml4 = pmm_alloc();
    if (!pml4) return NULL;
    kmemset(pml4, 0, PAGE_SIZE);

    if (kernel_pml4) {
        for (int i = 256; i < 512; i++)
            pml4[i] = kernel_pml4[i];

        /* The kernel executes from the identity map built by boot.asm
         * (PML4 entry 0: 512 x 2 MiB huge pages covering the low 1 GiB),
         * not from the higher half. Copy that subtree as well, or the
         * first CR3 switch into a fresh address space page-faults on the
         * next kernel instruction fetch. Only the table pages are
         * duplicated: the 2 MiB leaves still map the same physical
         * frames, and splits performed by get_or_alloc() for user
         * mappings only ever touch this address space's own tables. */
        if (kernel_pml4[0] & VMM_PRESENT) {
            uint64_t *kpdpt = (uint64_t *)(kernel_pml4[0] & ~0xFFFULL);
            uint64_t *pdpt = pmm_alloc();
            if (!pdpt) {
                pmm_free(pml4);
                return NULL;
            }
            for (int p3 = 0; p3 < 512; p3++) {
                if (!(kpdpt[p3] & VMM_PRESENT)) {
                    pdpt[p3] = 0;
                    continue;
                }
                uint64_t *kpdt = (uint64_t *)(kpdpt[p3] & ~0xFFFULL);
                uint64_t *pdt = pmm_alloc();
                if (!pdt) {
                    pdpt[p3] = 0;
                    continue;
                }
                kmemcpy(pdt, kpdt, PAGE_SIZE);
                /* User pages live under entry 0 too (USER_LOAD_BASE is
                 * 4 MiB): the walk must be permitted at link level, so
                 * promote the links to U/S. The copied 2 MiB leaves stay
                 * supervisor-only, which is what actually protects the
                 * kernel's identity-mapped RAM from ring 3. */
                pdpt[p3] = ((uint64_t)pdt & ~0xFFFULL)
                        | (kpdpt[p3] & 0xFFF) | VMM_USER;
            }
            pml4[0] = ((uint64_t)pdpt & ~0xFFFULL)
                    | (kernel_pml4[0] & 0xFFF) | VMM_USER;
        }
    }
    return pml4;
}

void
vmm_destroy_pml4(uint64_t *pml4)
{
    if (!pml4) return;
    for (int p4 = 0; p4 < 256; p4++) {
        if (!(pml4[p4] & VMM_PRESENT)) continue;
        uint64_t *pdpt = (uint64_t *)(pml4[p4] & ~0xFFFULL);
        for (int p3 = 0; p3 < 512; p3++) {
            if (!(pdpt[p3] & VMM_PRESENT)) continue;
            uint64_t *pdt = (uint64_t *)(pdpt[p3] & ~0xFFFULL);
            for (int p2 = 0; p2 < 512; p2++) {
                if (!(pdt[p2] & VMM_PRESENT)) continue;
                /* Huge entries map shared kernel frames (boot identity
                 * map): never free them or walk them as page tables. */
                if (pdt[p2] & VMM_HUGE) continue;
                uint64_t *pt = (uint64_t *)(pdt[p2] & ~0xFFFULL);
                for (int p1 = 0; p1 < 512; p1++) {
                    /* Free user pages only: kernel-identity leaves (the
                     * 4 KiB splits of the boot map) map shared RAM. */
                    if ((pt[p1] & VMM_PRESENT) && (pt[p1] & VMM_USER))
                        pmm_free((void *)(pt[p1] & ~0xFFFULL));
                }
                pmm_free(pt);
            }
            pmm_free(pdt);
        }
        pmm_free(pdpt);
    }
    pmm_free(pml4);
}

void
vmm_init(void)
{
    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    kernel_pml4 = (uint64_t *)(cr3 & ~0xFFFULL);
    
    slab_init();
    slab_ready = true;
}

void *
kmalloc(size_t size)
{
    if (slab_ready) {
        return kmalloc_slab(size);
    } else {
        static uint8_t *heap_cur = NULL;
        static size_t heap_left = 0;
        
        size = (size + 15) & ~(size_t)15;
        if (heap_left < size) {
            /* Refill with `pages` PHYSICALLY CONTIGUOUS pages: the bump
             * heap is a flat range, so claiming `pages * PAGE_SIZE` bytes
             * from a single page overruns into unowned memory. At boot
             * the PMM is pristine (no frees yet), so consecutive
             * pmm_alloc() calls are contiguous; if they ever are not,
             * release what we took and fail instead of corrupting. */
            size_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
            uint8_t *first = pmm_alloc();
            if (!first) return NULL;
            size_t got = 1;
            for (; got < pages; got++) {
                void *extra = pmm_alloc();
                if (!extra ||
                    (uint8_t *)extra != first + got * PAGE_SIZE) {
                    if (extra) pmm_free(extra);
                    break;
                }
            }
            if (got < pages) {
                for (size_t i = 0; i < got; i++)
                    pmm_free(first + i * PAGE_SIZE);
                return NULL;
            }
            heap_cur  = first;
            heap_left = pages * PAGE_SIZE;
        }
        void *ptr = heap_cur;
        heap_cur  += size;
        heap_left -= size;
        return ptr;
    }
}

void *
kmalloc_zero(size_t size)
{
    void *p = kmalloc(size);
    if (p) kmemset(p, 0, size);
    return p;
}

void
kfree(void *ptr)
{
    if (!ptr) return;
    if (slab_ready) {
        kfree_slab(ptr);
    }
}

/*
 * vmm_clone_pml4 — deep copy of user address space for fork()
 *
 * Walks PML4 entries 0-255 (user space only).
 * For every present page: allocates a fresh physical page,
 * copies the content, and maps it at the same virtual address
 * in the new PML4.  Kernel mappings (entries 256-511) are
 * inherited by vmm_new_pml4() automatically.
 */
uint64_t *
vmm_clone_pml4(uint64_t *src)
{
    uint64_t *dst = vmm_new_pml4();
    if (!dst) return NULL;

    for (int p4 = 0; p4 < 256; p4++) {
        if (!(src[p4] & VMM_PRESENT)) continue;

        uint64_t *src_pdpt = (uint64_t *)(src[p4] & ~0xFFFULL);

        for (int p3 = 0; p3 < 512; p3++) {
            if (!(src_pdpt[p3] & VMM_PRESENT)) continue;

            uint64_t *src_pdt = (uint64_t *)(src_pdpt[p3] & ~0xFFFULL);

            for (int p2 = 0; p2 < 512; p2++) {
                if (!(src_pdt[p2] & VMM_PRESENT)) continue;
                /* Huge entries are the kernel identity map: shared, not
                 * user pages — never copy or walk them as page tables. */
                if (src_pdt[p2] & VMM_HUGE) continue;

                uint64_t *src_pt = (uint64_t *)(src_pdt[p2] & ~0xFFFULL);

                for (int p1 = 0; p1 < 512; p1++) {
                    if (!(src_pt[p1] & VMM_PRESENT)) continue;
                    /* Copy user pages only; kernel-identity leaves are
                     * shared RAM inherited via vmm_new_pml4(). */
                    if (!(src_pt[p1] & VMM_USER)) continue;

                    uint64_t src_phys = src_pt[p1] & ~0xFFFULL;
                    uint64_t flags    = src_pt[p1] &  0xFFFULL;

                    /* Allocate new page and copy content */
                    void *new_page = pmm_alloc();
                    if (!new_page) {
                        vmm_destroy_pml4(dst);
                        return NULL;
                    }
                    kmemcpy(new_page, (void *)src_phys, PAGE_SIZE);

                    /* Reconstruct virtual address from table indices */
                    uint64_t virt = ((uint64_t)p4 << 39)
                                  | ((uint64_t)p3 << 30)
                                  | ((uint64_t)p2 << 21)
                                  | ((uint64_t)p1 << 12);

                    vmm_map(dst, virt, (uint64_t)new_page, flags);
                }
            }
        }
    }
    return dst;
}
