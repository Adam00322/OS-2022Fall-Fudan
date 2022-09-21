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

static QueueNode* root[4][12-4];
// define_early_init(init_root)
// {
//     for(int i=0; i<12-4; i++){
//         for(int j=0; j<4; j++){
//             root[j][i] = NULL;
//         }
//     }
// }

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
    int bit = mylog2(length)-4;
    auto pre = root[cpuid()][bit];
    auto p = MIN(p1, p2);
    if(pre == NULL){
        add_to_queue(&root[cpuid()][bit], p1);
        return;
    }else if(pre == p2){
        fetch_from_queue(&root[cpuid()][bit]);
        if(bit == 11-4){
            kfree_page(p);
        }else{
            length <<= 1;
            add_to_queue(&root[cpuid()][bit+1], p);
        }
        return;
    }else{
        while(pre->next && pre->next != p2){
            pre = pre->next;
        }
        if(pre->next != NULL){
            pre->next = pre->next->next;
            if(bit == 11-4){
                kfree_page(p);
            }else{
                add_to_queue(&root[cpuid()][bit+1], p);
            }
        }else{
            add_to_queue(&root[cpuid()][bit], p1);
        }
    }
    
}

void* kalloc(isize size)
{
    int bit = mylog2(size+sizeof(isize));
    int i;
    for(i=bit; i<12; i++){
        if(root[cpuid()][i-4] != NULL) break;
    }
    if(i >= 12){
        void* temp = kalloc_page();
        add_to_queue(&root[cpuid()][i-1-4], temp);
        add_to_queue(&root[cpuid()][i-1-4], temp + BIT(i-1));
        i--;
    }
    while(i > bit){
        void* temp = fetch_from_queue(&root[cpuid()][i-4]);
        add_to_queue(&root[cpuid()][i-1-4], temp);
        add_to_queue(&root[cpuid()][i-1-4], temp + BIT(i-1));
        i--;
    }
    isize* mem = (isize*)fetch_from_queue(&root[cpuid()][bit-4]);
    *mem = BIT(bit)-sizeof(isize);
    return mem + 1;
}

void kfree(void* p)
{
    if(p == NULL)
        return;
    p -= sizeof(isize);
    isize length = *(isize*)p+sizeof(isize);
    if((isize)p % (length << 1) == 0){
        merge(p, p+length, length);
    }else{
        merge(p, p-length, length);
    }
    // add_to_queue(&root[cpuid()][mylog2(length)-4], p);
}