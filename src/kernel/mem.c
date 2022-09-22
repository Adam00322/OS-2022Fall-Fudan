#include <common/rc.h>
#include <common/list.h>
#include <kernel/init.h>
#include <kernel/mem.h>
#include <driver/memlayout.h>
#include <common/spinlock.h>
#include <kernel/printk.h>

RefCount alloc_page_cnt;

define_early_init(alloc_page_cnt)
{
    init_rc(&alloc_page_cnt);
}

static QueueNode* pages;
extern char end[];
define_early_init(pages)
{
    for (u64 p = PAGE_BASE((u64)&end) + PAGE_SIZE; p < P2K(PHYSTOP); p += PAGE_SIZE)
	   add_to_queue(&pages, (QueueNode*)p); 
}

struct block {
    isize length;
	struct block * next;
};
static struct block free_list[4];
define_early_init(init_free_list)
{
    free_list[cpuid()].length = 0;
    free_list[cpuid()].next = NULL;
}

static SpinLock* lock;
define_early_init(init_lock)
{
    init_spinlock(lock);
}

void* kalloc_page()
{
    _increment_rc(&alloc_page_cnt);
    // TODO
    return fetch_from_queue(&pages);
}

void kfree_page(void* p)
{
    _decrement_rc(&alloc_page_cnt);
    // TODO
    add_to_queue(&pages, (QueueNode*)p);
}

// TODO: kalloc kfree
struct block* _add_page()
{
    struct block* it = (struct block*)kalloc_page();
    it->next = NULL;
    it->length = PAGE_SIZE - sizeof(isize);
    return it;
}

void merge(struct block* cur){
    struct block* next = (struct block*)((void*)cur + cur->length + sizeof(isize));
    auto pre = &free_list[cpuid()];
    if((next->length & 1) == 1 || (isize)next % PAGE_SIZE == 0){//notfree
        cur->next = free_list[cpuid()].next;
        free_list[cpuid()].next = cur;
        return;
    }
    while(pre->next != next){
        pre = pre->next;
        if((void*)pre->next + pre->next->length + sizeof(isize) == cur){
            next = cur;
            cur = pre->next;
            next->next = cur->next;
            break;
        }
    }
    cur->length += next->length + sizeof(isize);
    if(cur->length == PAGE_SIZE - sizeof(isize)){
        pre->next = next->next;
        kfree_page(cur);
        return;
    }
    pre->next = next->next;
    merge(cur);
}

void* kalloc(isize size)
{
    struct block* prev = &(free_list[cpuid()]);
    struct block* cur = free_list[cpuid()].next;
    size = (size + 7) & ~7;

    while(cur != NULL){
		if (cur->length >= size) break;
		prev = cur;
		cur = cur->next;
	}
    if(cur == NULL){
        cur = _add_page();
    }
    
    if((cur->length - size) >= 16){
		struct block * temp = (struct block *)((void*)cur + size + sizeof(isize));
		temp->next = cur->next;
		temp->length = cur->length - size - sizeof(isize);
		prev->next = temp;
		cur->length = size;
	}else{
		prev->next = cur->next;
	}
    cur->length++;//notfree
    return (void*)cur + sizeof(isize);
}

void kfree(void* p)
{
    if(p == NULL)
        return;
    struct block* cur = p - sizeof(isize);
    cur->length--;//free
    merge(cur);
    // cur->next = free_list[cpuid()].next;
    // free_list[cpuid()].next = cur;

}