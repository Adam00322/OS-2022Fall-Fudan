#include <common/rc.h>
#include <common/list.h>
#include <kernel/init.h>
#include <kernel/mem.h>
#include <driver/memlayout.h>
#include <common/string.h>
#include <fs/cache.h>

RefCount alloc_page_cnt;

define_early_init(alloc_page_cnt)
{
    init_rc(&alloc_page_cnt);
}

static QueueNode* pages;
extern char end[];
define_early_init(pages)
{
    for (u64 p = PAGE_BASE((u64)&end) + PAGE_SIZE; p < P2K(PHYSTOP); p += PAGE_SIZE){
	   add_to_queue(&pages, (QueueNode*)p); 
        _increment_rc(&alloc_page_cnt);
    }
    fetch_from_queue(&pages);
    _decrement_rc(&alloc_page_cnt);
}

static void* zero_page;
define_init(zero_page){
    zero_page = kalloc_page();
    memset(zero_page, 0, PAGE_SIZE);
}

#define N 16
static QueueNode* slab[PAGE_SIZE/N];
static struct page page_ref[PHYSTOP/PAGE_SIZE];

void* kalloc_page()
{
    _decrement_rc(&alloc_page_cnt);
    auto p = fetch_from_queue(&pages);
    auto page = page_ref[K2P(p) / PAGE_SIZE];
    page.ref = 1;
    init_spinlock(&page.lock);
    return p;
}

void kfree_page(void* p)
{
    _increment_rc(&alloc_page_cnt);
    auto page = page_ref[K2P(p) / PAGE_SIZE];
    _acquire_spinlock(&page.lock);
    page.ref--;
    if(page.ref == 0){
        add_to_queue(&pages, (QueueNode*)p);
    }
    _release_spinlock(&page.lock);
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

u64 left_page_cnt(){
    return alloc_page_cnt.count;
}

void* get_zero_page(){
    return zero_page;
}

u32 write_page_to_disk(void* ka){
    u32 bno = find_and_set_8_blocks();
    for(u32 i = 0; i < 8; i++){
        block_device.write(bno + i, ka + i * BLOCK_SIZE);
    }
    return bno;
}

void read_page_from_disk(void* ka, u32 bno){
    for(u32 i = 0; i < 8; i++){
        block_device.read(bno + i, ka + i * BLOCK_SIZE);
    }
    release_8_blocks(bno);
}