#include <common/rc.h>
#include <common/list.h>
#include <kernel/init.h>
#include <kernel/mem.h>
#include <driver/memlayout.h>

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

#define N 16
static QueueNode* slab[PAGE_SIZE/N];

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


void _add_page(isize size)
{
    void* it = kalloc_page();
    void* _end = it + PAGE_SIZE;
    QueueNode** pslab = &slab[size/N];
    *(isize*)it = size;
    for (it = it + sizeof(isize); it + size < _end; it += size)
	    add_to_queue(pslab, (QueueNode*)it);
}

void* kalloc(isize size)
{
    size = (size + N-1) & ~(N-1);
    void* mem;
    while((mem = fetch_from_queue(&slab[size/N])) == NULL){
        _add_page(size);
    }
    return mem;
}

void kfree(void* p)
{
    if(p == NULL)
        return;
    auto head = (isize)p & ~(PAGE_SIZE-1);
    isize size = *(isize*)head;
    add_to_queue(&slab[size/N], (QueueNode*)p);
}
