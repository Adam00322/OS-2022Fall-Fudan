#include <kernel/proc.h>
#include <aarch64/mmu.h>
#include <fs/block_device.h>
#include <fs/cache.h> 
#include <kernel/paging.h>
#include <common/defines.h>
#include <kernel/pt.h>
#include <common/sem.h>
#include <common/list.h>
#include <kernel/mem.h>
#include <kernel/sched.h>
#include <common/string.h>
#include <kernel/printk.h>
#include <kernel/init.h>

// define_rest_init(paging){
// 	//TODO init
// 	init_block_device();
// 	init_bcache(get_super_block(), &block_device);
// }

static struct section* init_heap(ListNode* section_head, u64 begin){
	struct section* s = kalloc(sizeof(struct section));
	s->flags = ST_HEAP;
	init_sleeplock(&s->sleeplock);
	s->end = s->begin = begin;
	_insert_into_list(section_head, &s->stnode);
	return s;
}

u64 sbrk(i64 size){
	//TODO
	auto pd = &thisproc()->pgdir;
	u64 begin = 0;
	_for_in_list(p, &pd->section_head){
		if(p == &pd->section_head) continue;
		auto section = container_of(p, struct section, stnode);
		if(section->flags & ST_HEAP){
			u64 end = section->end;
			if(size >= 0){
				section->end += size*PAGE_SIZE;
			}else{
				size = -1 * size;
				if(section->end < size*PAGE_SIZE + section->begin) PANIC();
				if(section->flags & ST_SWAP){
					swapin(pd, section);
				}
				section->end -= size*PAGE_SIZE;
				for(u64 va = section->end; va < end; va += PAGE_SIZE){
					auto pte = get_pte(pd, va, false);
					if(pte != NULL && (*pte & PTE_VALID)){
						void* ka = (void*)P2K(PTE_ADDRESS(*pte));
						kfree_page(ka);
						*pte = NULL;
					}
				}
				arch_tlbi_vmalle1is();
			}
			return end;
		}
		begin = MAX(begin, section->end);
	}
	if(size < 0) PANIC();
	auto s = init_heap(&pd->section_head, PAGE_BASE(begin) + 5 * PAGE_SIZE);
	s->end += size*PAGE_SIZE;
	return s->end;
}	


void* alloc_page_for_user(){
	while(left_page_cnt() <= REVERSED_PAGES){ //this is a soft limit
		//TODO
		auto pd = &get_offline_proc()->pgdir;
		if(pd == NULL) return NULL;
		_for_in_list(p, &pd->section_head){
			if(p == &pd->section_head) continue;
			auto st = container_of(p, struct section, stnode);
			if(st->flags == ST_HEAP){
				swapout(pd, st);
			}
		}
	}
	return kalloc_page();
}

//caller must have the pd->lock
void swapout(struct pgdir* pd, struct section* st){
	ASSERT(!(st->flags & ST_SWAP));
	st->flags |= ST_SWAP;
	//TODO
	pd->online = true;
	u64 begin = st->begin;
	u64 end = st->end;
	for(u64 va = begin; va < end; va += PAGE_SIZE){
		auto pte = get_pte(pd, va, false);
		if(pte != NULL){
			*pte = *pte & (~PTE_VALID);
		}
	}
	setup_checker(out);
	unalertable_acquire_sleeplock(out, &st->sleeplock);
	_release_spinlock(&pd->lock);
	if(st->flags & ST_FILE){
		// TODO
	}else{
		for(u64 va = begin; va < end; va += PAGE_SIZE){
			auto pte = get_pte(pd, va, false);
			if(pte != NULL && *pte != NULL){
				void* ka = (void*)P2K(PTE_ADDRESS(*pte));
				*pte = write_page_to_disk(ka) << 12;
				kfree_page(ka);
			}
		}
	}
	release_sleeplock(out, &st->sleeplock);
}
//Free 8 continuous disk blocks
void swapin(struct pgdir* pd, struct section* st){
	ASSERT(st->flags & ST_SWAP);
	//TODO
	setup_checker(in);
	unalertable_acquire_sleeplock(in, &st->sleeplock);
	for(u64 va = st->begin; va < st->end; va += PAGE_SIZE){
		auto pte = get_pte(pd, va, false);
		if(pte != NULL && *pte != NULL){
			void* ka = alloc_page_for_user();
			read_page_from_disk(ka, (*pte) >> 12);
			vmmap(pd, va, ka, PTE_USER_DATA);
		}
	}
	release_sleeplock(in, &st->sleeplock);
	st->flags &= ~ST_SWAP;
}

int pgfault(u64 iss){
	iss = iss;
	struct proc* p = thisproc();
	struct pgdir* pd = &p->pgdir;
	u64 addr = arch_get_far();
	//TODO
	struct section* st = NULL;
	_for_in_list(np, &pd->section_head){
		if(np == &pd->section_head) continue;
		auto s = container_of(np, struct section, stnode);
		if(addr >= s->begin && addr < s->end){
			st = s;
			break;
		}
	}
	ASSERT(st);
	auto pte = get_pte(pd, addr, true);
	if((*pte & PTE_VALID) == 0){
		if(st->flags & ST_FILE){
			auto inode = st->fp->ip;
			auto ka = alloc_page_for_user();
			inodes.read(inode, ka, st->offset+PAGE_BASE(addr)-st->begin, PAGE_SIZE);
			u64 flags = PTE_USER_DATA;
			if(st->flags & ST_RO) flags |= PTE_RO;
			vmmap(pd, addr, ka, flags);
		}else{
			if(st->flags & ST_SWAP)
				swapin(pd, st);
			if(*pte == NULL)
				vmmap(pd, addr, alloc_page_for_user(), PTE_USER_DATA);
		}
	}else if((*pte) & PTE_RO){
		auto ka = alloc_page_for_user();
		memcpy(ka, (void*)P2K(PTE_ADDRESS(*pte)), PAGE_SIZE);
		kfree_page((void*)P2K(PTE_ADDRESS(*pte)));
		vmmap(pd, addr, ka, PTE_USER_DATA);
	}else{
		PANIC();
	}
	arch_tlbi_vmalle1is();
	return 0;
}

void init_sections(ListNode* section_head){
	init_list_node(section_head);
	// struct section* s = kalloc(sizeof(struct section));
	// s->flags = ST_HEAP;
	// init_sleeplock(&s->sleeplock);
	// s->end = s->begin = 0x0;
	// _insert_into_list(section_head, &s->stnode);
}

void free_sections(struct pgdir* pd){
	ListNode* pre = NULL;
	_for_in_list(p, &pd->section_head){
		if(p == &pd->section_head) continue;
		auto section = container_of(p, struct section, stnode);
		if(section->flags & ST_SWAP){
			swapin(pd, section);
		}
		for(u64 va = section->begin; va < section->end; va += PAGE_SIZE){
			auto pte = get_pte(pd, va, false);
			if(pte != NULL && (*pte & PTE_VALID)){
				void* ka = (void*)P2K(PTE_ADDRESS(*pte));
				kfree_page(ka);
			}
		}
		if(pre != NULL) kfree(pre);
		pre = p;
	}
	if(pre != NULL) kfree(pre);
}

void copy_sections(ListNode* from_head, ListNode* to_head){
	init_list_node(to_head);
	_for_in_list(p, from_head){
		if(p == from_head) continue;
		struct section* s = kalloc(sizeof(struct section));
		*s = *container_of(p, struct section, stnode);
		_insert_into_list(to_head, &s->stnode);
	}
}