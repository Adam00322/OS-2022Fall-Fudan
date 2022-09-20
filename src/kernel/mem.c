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
	struct block * next;
	int length;
    int free;
};
static struct block free_list[4];
define_early_init(init_free_list)
{
    free_list[cpuid()].length = 0;
    free_list[cpuid()].next = NULL;
    free_list[cpuid()].free = 0;
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
    it->length = PAGE_SIZE - sizeof(struct block);
    it->free = 1;
    return it;
}

void merge(struct block* cur){
    struct block* next = (struct block*)((void*)cur + cur->length + sizeof(struct block));
    if(next != NULL && next->free == 1){
        auto it = &free_list[cpuid()];
        while(it !=NULL && it->next != next){
            it = it->next;
        }
        cur->length += next->length + sizeof(struct block);
        if(cur->length == PAGE_SIZE - sizeof(struct block) && (i64)cur % PAGE_SIZE == 0){
            it->next = next->next;
            kfree_page(cur);
            return;
        }
        it->next = cur;
        cur->next = next->next;
    }else{
        cur->next = free_list[cpuid()].next;
        free_list[cpuid()].next = cur;
    }
    cur->free = 1;
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
		struct block * temp = (struct block *)((void*)cur + size + sizeof(struct block));
		temp->next = cur->next;
		temp->length = cur->length - size - sizeof(struct block);
        temp->free = 1;
		prev->next = temp;
		cur->length = size;
	}else{
		prev->next = cur->next;
	}
    cur->free = 0;

    return (void*)cur + sizeof(struct block);
}

void kfree(void* p)
{
    if(p == NULL)
        return;
    // printk("1");
    struct block* cur = (struct block *)(p - sizeof(struct block));
    merge(cur);

}