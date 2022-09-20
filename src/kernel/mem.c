#include <common/rc.h>
#include <common/list.h>
#include <kernel/init.h>
#include <kernel/mem.h>
#include <driver/memlayout.h>
#include <common/spinlock.h>
#include <kernel/printk.h>
#include <common/rbtree.h>

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

rb_root root[12];
define_early_init(init_rb_tree)
{
    static struct rb_root_ r[12];
    static struct rb_node_ node[12];
    for(int i=0; i<12; i++){
        root[i] = &(r[i]);
        root[i]->rb_node = &(node[i]);
    }
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
int mylog2(isize num){
    isize t = 1;
    int bit = 0;
    while(t < num){
        t <<= 1;
        bit ++;
    }
    return bit;
}

bool cmp(rb_node lnode,rb_node rnode){
    return lnode < rnode;
}

void* kalloc(isize size)
{
    int bit = mylog2(size+8);
    int i;
    printk("1\n");
    _acquire_spinlock(lock);
    for(i=bit; i<12; i++){
        if(_rb_first(root[i]) != NULL) break;
    }
    printk("2\n");
    if(i >= 12){
        printk("2.1\n");
        void* temp = kalloc_page();
        printk("2.2\n");
        _rb_insert(temp, root[i-1], cmp);
        printk("2.4\n");
        _rb_insert(temp + BIT(i-1), root[i-1], cmp);
        i--;
    }
    printk("3\n");
    while(i > bit){
        void* temp = (void*)_rb_first(root[i]);
        _rb_erase(temp, root[i]);
        _rb_insert(temp, root[i-1], cmp);
        _rb_insert(temp + BIT(i-1), root[i-1], cmp);
        i--;
    }
    printk("4\n");
    void* mem = _rb_first(root[i]);
    _rb_erase(mem, root[i]);
    _release_spinlock(lock);
    *(isize*)mem = BIT(i);
    return mem + sizeof(isize);
}

void kfree(void* p)
{
    if(p == NULL)
        return;
    p -= sizeof(isize);
    int bit = mylog2(*(isize*)p);
    _acquire_spinlock(lock);
    _rb_insert((rb_node)p, root[bit], cmp);
    _release_spinlock(lock);
}