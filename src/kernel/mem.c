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

#define unuse 4
static QueueNode* root[12-unuse];

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
inline int mylog2(isize num){
    isize t = 1;
    int bit = 0;
    while(t < num){
        t <<= 1;
        bit ++;
    }
    return bit;
}

void merge(QueueNode* p1, QueueNode* p2, isize length){
    int bit = mylog2(length)-unuse;
    auto pre = root[bit];
    auto p = MIN(p1, p2);
    if(pre == NULL){
        add_to_queue(&root[bit], p1);
        return;
    }else if(pre == p2){
        fetch_from_queue(&root[bit]);
        if(bit == 11-unuse){
            kfree_page(p);
        }else{
            length <<= 1;
            add_to_queue(&root[bit+1], p);
        }
        return;
    }else{
        while(pre->next && pre->next != p2){
            pre = pre->next;
        }
        if(pre->next != NULL){
            pre->next = pre->next->next;
            if(bit == 11-unuse){
                kfree_page(p);
            }else{
                add_to_queue(&root[bit+1], p);
            }
        }else{
            add_to_queue(&root[bit], p1);
        }
    }
    
}

void split(int bit){
    int i;
    for(i=bit; i<12; i++){
        if(root[i-unuse] != NULL) break;
    }
    if(i >= 12){
        void* temp = kalloc_page();
        add_to_queue(&root[i-1-unuse], temp);
        add_to_queue(&root[i-1-unuse], temp + BIT(i-1));
        i--;
    }
    while(i > bit){
        void* temp = fetch_from_queue(&root[i-unuse]);
        if(temp == NULL) break;
        add_to_queue(&root[i-1-unuse], temp);
        add_to_queue(&root[i-1-unuse], temp + BIT(i-1));
        i--;
    }
}

void* kalloc(isize size)
{
    int bit = mylog2(size+sizeof(isize));
    isize* mem;
    if(bit >= 12){
        mem = kalloc_page();
    }else{
        while((mem = (isize*)fetch_from_queue(&root[bit-unuse])) == NULL){
            split(bit);
        }
    }
    *mem = BIT(bit)-sizeof(isize);
    return mem + 1;
}

void kfree(void* p)
{
    if(p == NULL)
        return;
    p -= sizeof(isize);
    isize length = *(isize*)p+sizeof(isize);
    // _acquire_spinlock(lock);
    // if((isize)p % (length << 1) == 0){
    //     merge(p, p+length, length);
    // }else{
    //     merge(p, p-length, length);
    // }
    // _release_spinlock(lock);
    add_to_queue(&root[mylog2(length)-unuse], p);
}