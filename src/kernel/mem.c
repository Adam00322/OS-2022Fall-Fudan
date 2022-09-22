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
static struct block free_list[4][12];

static SpinLock* lock;
define_early_init(init_lock)
{
    init_spinlock(lock);
}

void* kalloc_page()
{
    _increment_rc(&alloc_page_cnt);
    return fetch_from_queue(&pages);
}

void kfree_page(void* p)
{
    _decrement_rc(&alloc_page_cnt);
    add_to_queue(&pages, (QueueNode*)p);
}


struct block* _add_page()
{
    struct block* it = (struct block*)kalloc_page();
    it->next = NULL;
    it->length = PAGE_SIZE - sizeof(isize);
    return it;
}

inline int mylog2(isize num){//lowerbound
    int bit = 0;
    while(num > 1){
        num >>= 1;
        bit ++;
    }
    return bit;
}

void merge(struct block* cur){
    struct block* next = (struct block*)((void*)cur + cur->length + sizeof(isize));
    if((next->length & 1) == 1 || (isize)next % PAGE_SIZE == 0){//notfree
        cur->next = free_list[cpuid()][mylog2(cur->length)].next;
        free_list[cpuid()][mylog2(cur->length)].next = cur;
        return;
    }
    auto pre = &free_list[cpuid()][mylog2(next->length)];
    while(pre->next != next){
        pre = pre->next;
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
    size = (size + 7) & ~7;
    int bit = mylog2(size);
    struct block* prev = &(free_list[cpuid()][bit]);
    struct block* cur = free_list[cpuid()][bit].next;

    for(int i = bit; i<12; i++){
        prev = &(free_list[cpuid()][i]);
        cur = free_list[cpuid()][i].next;
        while(cur != NULL){
            if (cur->length >= size) break;
            prev = cur;
            cur = cur->next;
        }
        if(cur != NULL) break;
    }

    if(cur == NULL){
        cur = _add_page();
    }
    prev->next = cur->next;
    if((cur->length - size) >= 16){
		struct block * temp = (struct block *)((void*)cur + size + sizeof(isize));
		temp->length = cur->length - size - sizeof(isize);
        prev = &(free_list[cpuid()][mylog2(temp->length)]);
        temp->next = prev->next;
		prev->next = temp;
		cur->length = size;
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
}