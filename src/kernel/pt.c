#include <kernel/pt.h>
#include <kernel/mem.h>
#include <common/string.h>
#include <aarch64/intrinsic.h>
#include <kernel/paging.h>
#include <kernel/sched.h>
#include <kernel/printk.h>

PTEntriesPtr get_pte(struct pgdir* pgdir, u64 va, bool alloc)
{
    // TODO
    // Return a pointer to the PTE (Page Table Entry) for virtual address 'va'
    // If the entry not exists (NEEDN'T BE VALID), allocate it if alloc=true, or return NULL if false.
    // THIS ROUTINUE GETS THE PTE, NOT THE PAGE DESCRIBED BY PTE.
    PTEntriesPtr pt0 = NULL;
    PTEntriesPtr pt1 = NULL;
    PTEntriesPtr pt2 = NULL;
    PTEntriesPtr pt3 = NULL;
    if((pt0 = pgdir->pt) != NULL){
        if(pt0[VA_PART0(va)] & PTE_VALID){
            pt1 = (PTEntriesPtr)P2K(PTE_ADDRESS(pt0[VA_PART0(va)]));
            if(pt1[VA_PART1(va)] & PTE_VALID){
                pt2 = (PTEntriesPtr)P2K(PTE_ADDRESS(pt1[VA_PART1(va)]));
                if(pt2[VA_PART2(va)] & PTE_VALID){
                    pt3 = (PTEntriesPtr)P2K(PTE_ADDRESS(pt2[VA_PART2(va)]));
                    return &pt3[VA_PART3(va)];
                }
            }
        }
    }
    
    if(alloc){
        if(pt0 == NULL){
            pgdir->pt = pt0 = kalloc_page();
            memset(pt0, 0, PAGE_SIZE);
        }
        if(pt1 == NULL){
            pt1 = kalloc_page();
            memset(pt1, 0, PAGE_SIZE);
            pt0[VA_PART0(va)] = K2P(pt1) | PTE_TABLE;
        }
        if(pt2 == NULL){
            pt2 = kalloc_page();
            memset(pt2, 0, PAGE_SIZE);
            pt1[VA_PART1(va)] = K2P(pt2) | PTE_TABLE;
        }
        if(pt3 == NULL){
            pt3 = kalloc_page();
            memset(pt3, 0, PAGE_SIZE);
            pt2[VA_PART2(va)] = K2P(pt3) | PTE_TABLE;
        }
        return &pt3[VA_PART3(va)];
    }

    return NULL;
}

void init_pgdir(struct pgdir* pgdir)
{
    pgdir->pt = kalloc_page();
    memset(pgdir->pt, 0, PAGE_SIZE);
    init_spinlock(&pgdir->lock);
    init_sections(&pgdir->section_head);
    pgdir->online = false;
}

void vmmap(struct pgdir* pd, u64 va, void* ka, u64 flags)
{
    PTEntriesPtr pt = get_pte(pd, va, true);
    *pt = K2P((u64)ka|flags);
    increment_ref(ka);
}

void free_pgdir(struct pgdir* pgdir)
{
    // TODO
    // Free pages used by the page table. If pgdir->pt=NULL, do nothing.
    // DONT FREE PAGES DESCRIBED BY THE PAGE TABLE
    if(pgdir->pt != NULL){
        free_sections(pgdir);
        PTEntriesPtr pt0 = pgdir->pt;
        for(int i = 0; i < N_PTE_PER_TABLE; i++){
            if(pt0[i] & PTE_VALID){
                PTEntriesPtr pt1 = (PTEntriesPtr)P2K(PTE_ADDRESS(pt0[i]));
                for(int j = 0; j < N_PTE_PER_TABLE; j++){
                    if(pt1[j] & PTE_VALID){
                        PTEntriesPtr pt2 = (PTEntriesPtr)P2K(PTE_ADDRESS(pt1[j]));
                        for(int k = 0; k < N_PTE_PER_TABLE; k++){
                            if(pt2[k] & PTE_VALID) kfree_page((void *)P2K(PTE_ADDRESS(pt2[k])));
                        }
                        kfree_page(pt2);
                    }
                }
                kfree_page(pt1);
            }
        }
        kfree_page(pt0);
        pgdir->pt = NULL;
    }
}

void attach_pgdir(struct pgdir* pgdir)
{
    extern PTEntries invalid_pt;
    auto thispd = &thisproc()->pgdir;
    _acquire_spinlock(&thispd->lock);
    thispd->online = false;
    _release_spinlock(&thispd->lock);
    if(pgdir->pt){
        arch_set_ttbr0(K2P(pgdir->pt));
        _acquire_spinlock(&pgdir->lock);
        pgdir->online = true;
        _release_spinlock(&pgdir->lock);
    }else{
        arch_set_ttbr0(K2P(&invalid_pt));
    }
    arch_tlbi_vmalle1is();
}

/*
 * Copy len bytes from p to user address va in page table pgdir.
 * Allocate physical pages if required.
 * Useful when pgdir is not the current page table.
 */
int copyout(struct pgdir* pd, void* va, void *p, usize len){
    // TODO
    if(len == 0) return 0;
    u64 offset = (u64)va;
    u64 end = offset+len;
    for(u64 i = offset/PAGE_SIZE; i <= (end-1)/PAGE_SIZE; i++){
        u64 n = MIN(end - offset, (i + 1) * PAGE_SIZE - offset);
        auto pte = get_pte(pd, offset, true);
        if((*pte & PTE_VALID) == 0){
            void* ka = alloc_page_for_user();
            vmmap(pd, offset, ka, PTE_USER_DATA);
        }
        memcpy((void*)P2K(PTE_ADDRESS(*pte))+offset-PAGE_BASE(offset), p, n);
        offset += n;
        p += n;
    }
    return 0;
    // struct pgdir* prepd = P2K(arch_get_ttbr0());
    // attach_pgdir(pd);
    // memcpy(va, p, len);
    // attach_pgdir(prepd);
}

