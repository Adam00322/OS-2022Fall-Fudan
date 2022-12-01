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

define_rest_init(paging){
	//TODO init		
}

u64 sbrk(i64 size){
	//TODO
	_for_in_list(p, &thisproc()->pgdir.section_head){
		if(p == &thisproc()->pgdir.section_head) continue;
		auto section = container_of(p, struct section, stnode);
		if(section->flags == ST_HEAP){
			u64 end = section->end;
			section->end += size*PAGE_SIZE;
			return end;
		}
	}
	PANIC();
}	


void* alloc_page_for_user(){
	while(left_page_cnt() <= REVERSED_PAGES){ //this is a soft limit
		//TODO
	}
	return kalloc_page();
}

//caller must have the pd->lock
void swapout(struct pgdir* pd, struct section* st){
	ASSERT(!(st->flags & ST_SWAP));
	st->flags |= ST_SWAP;
	//TODO

}
//Free 8 continuous disk blocks
void swapin(struct pgdir* pd, struct section* st){
	ASSERT(st->flags & ST_SWAP);
	//TODO
	st->flags &= ~ST_SWAP;
}

int pgfault(u64 iss){
	struct proc* p = thisproc();
	struct pgdir* pd = &p->pgdir;
	u64 addr = arch_get_far();
	//TODO
}

void init_sections(ListNode* section_head){
	struct section* s = kalloc(sizeof(struct section));
	s->flags = ST_HEAP;
	init_sleeplock(&s->sleeplock);
	s->begin = 0xc0000000;
	s->end = 0xc0000000;
	_insert_into_list(section_head, &s->stnode);
}

void free_sections(struct pgdir* pd){
	_for_in_list(p, &pd->section_head){
		if(p == &pd->section_head) continue;
		auto section = container_of(p, struct section, stnode);

	}
}