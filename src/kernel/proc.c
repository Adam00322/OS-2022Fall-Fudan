#include <kernel/proc.h>
#include <kernel/init.h>
#include <kernel/mem.h>
#include <kernel/sched.h>
#include <common/list.h>
#include <common/string.h>
#include <kernel/printk.h>

struct proc root_proc;

void kernel_entry();
void proc_entry();

static SpinLock tree_lock;
define_early_init(init_lock){
    init_spinlock(&tree_lock);
}

static hash_map h;
define_early_init(init_hash){
    h = kalloc(sizeof(struct hash_map_));
    _hashmap_init(h);
}

typedef struct hashpid
{
    int pid;
    struct proc* proc;
    struct hash_node_ node;
} hashpid_t;

int hash(hash_node node){
    return container_of(node, hashpid_t, node)->pid % HASHSIZE;
}

bool hashcmp(hash_node node1, hash_node node2){
    return container_of(node1, hashpid_t, node)->pid == container_of(node2, hashpid_t, node)->pid;
}

typedef struct pidmap
{
    unsigned int nr_free;
    char page[4096];
} pidmap_t;
#define PID_MAX_DEFAULT 0x8000
#define BITS_PER_BYTE 8
#define BITS_PER_PAGE (PAGE_SIZE * BITS_PER_BYTE)
#define BITS_PER_PAGE_MASK (BITS_PER_PAGE - 1)
static pidmap_t pidmap = { PID_MAX_DEFAULT, {0}};
static int last_pid = -1;

static int test_and_set_bit(int offset, void *addr)
{
    unsigned long mask = 1UL << (offset & (sizeof(unsigned long) * BITS_PER_BYTE - 1));
    unsigned long *p = ((unsigned long*)addr) + (offset >> (sizeof(unsigned long) + 1));
    unsigned long old = *p;
    *p = old | mask;
    return (old & mask) != 0;
}

static void clear_bit(int offset, void *addr)
{
    unsigned long mask = 1UL << (offset & (sizeof(unsigned long) * BITS_PER_BYTE - 1));
    unsigned long *p = ((unsigned long*)addr) + (offset >> (sizeof(unsigned long) + 1));
    unsigned long old = *p;
    *p = old & ~mask;
}

static int find_next_zero_bit(void *addr, int size, int offset)
{
    unsigned long *p;
    unsigned long mask;
    while (offset < size)
    {
        p = ((unsigned long*)addr) + (offset >> (sizeof(unsigned long) + 1));
        mask = 1UL << (offset & (sizeof(unsigned long) * BITS_PER_BYTE - 1));
 
        if ((~(*p) & mask))
        {
            break;
        }
        ++offset;
    }
    return offset;
}

static int alloc_pid()
{
    int pid = last_pid + 1;
    int offset = pid & BITS_PER_PAGE_MASK;
    if (!pidmap.nr_free)
    {
        return -1;
    }
    offset = find_next_zero_bit(&pidmap.page, BITS_PER_PAGE, offset);
    if(offset == BITS_PER_PAGE) offset = find_next_zero_bit(&pidmap.page, offset-1, 30);
    if (BITS_PER_PAGE != offset && !test_and_set_bit(offset, &pidmap.page))
    {
        --pidmap.nr_free;
        last_pid = offset;
        return offset;
    }
    return -1;
}

static int alloc_pidmap(struct proc* p)
{
    int pid = alloc_pid();
    if(pid == -1) return -1;
    hashpid_t* hashpid = kalloc(sizeof(hashpid_t));
    hashpid->pid = pid;
    hashpid->proc = p;
    _hashmap_insert(&hashpid->node, h, hash);
    return pid;
}

static void free_pidmap(int pid)
{
    int offset = pid & BITS_PER_PAGE_MASK;
 
    if(pid > 29)pidmap.nr_free++;
    clear_bit(offset, &pidmap.page);
    auto hashnode = _hashmap_lookup(&(hashpid_t){pid, NULL, {NULL}}.node, h, hash, hashcmp);
    _hashmap_erase(hashnode, h, hash);
    kfree(container_of(hashnode, hashpid_t, node));
}


void set_parent_to_this(struct proc* proc)
{
    // TODO: set the parent of proc to thisproc
    // NOTE: maybe you need to lock the process tree
    // NOTE: it's ensured that the old proc->parent = NULL
    _acquire_spinlock(&tree_lock);
    proc->parent = thisproc();
    _insert_into_list(&thisproc()->children, &proc->ptnode);
    _release_spinlock(&tree_lock);
}

NO_RETURN void exit(int code)
{
    // TODO
    // 1. set the exitcode
    // 2. clean up the resources
    // 3. transfer children to the root_proc, and notify the root_proc if there is zombie
    // 4. notify the parent
    // 5. sched(ZOMBIE)
    // NOTE: be careful of concurrency
    setup_checker(qwq);
    auto this = thisproc();
    this->exitcode = code;
    free_pgdir(&this->pgdir);
    //TODO clean up file resources
    _acquire_spinlock(&tree_lock);
    ListNode* pre = NULL;
    _for_in_list(p, &this->children){
        if(pre != NULL && pre != &this->children){
            auto proc = container_of(pre, struct proc, ptnode);
            proc->parent = &root_proc;
            auto t = &root_proc.children;
            if(is_zombie(proc)){
                pre->prev = t->prev;
                pre->next = t;
                t->prev->next = pre;
                t->prev = pre;
                post_sem(&root_proc.childexit);
            }else{
                _insert_into_list(t, pre);
            }
        }
        pre = p;
    }
    init_list_node(&this->children);
    pre = &this->ptnode;
    _detach_from_list(pre);
    auto t = &this->parent->children;
    pre->prev = t->prev;
    pre->next = t;
    t->prev->next = pre;
    t->prev = pre;
    post_sem(&this->parent->childexit);
    lock_for_sched(qwq);
    _release_spinlock(&tree_lock);
    sched(qwq, ZOMBIE);
    PANIC(); // prevent the warning of 'no_return function returns'
}

int wait(int* exitcode)
{
    // TODO
    // 1. return -1 if no children
    // 2. wait for childexit
    // 3. if any child exits, clean it up and return its pid and exitcode
    // NOTE: be careful of concurrency
    auto this = thisproc();
    if(_empty_list(&this->children))
        return -1;
    if(!wait_sem(&this->childexit)) return -1;
    _acquire_spinlock(&tree_lock);
    auto p = this->children.prev;
    auto proc = container_of(p, struct proc, ptnode);
    if(is_zombie(proc)){
        *exitcode = proc->exitcode;
        int id = proc->pid;
        _detach_from_list(p);
        kfree_page(proc->kstack);
        kfree(proc);
        free_pidmap(id);
        _release_spinlock(&tree_lock);
        return id;
    }
    _release_spinlock(&tree_lock);
    return -1;
}

int kill(int pid)
{
    // TODO
    // Set the killed flag of the proc to true and return 0.
    // Return -1 if the pid is invalid (proc not found).
    _acquire_spinlock(&tree_lock);
    auto p = _hashmap_lookup(&(hashpid_t){pid, NULL, {NULL}}.node, h, hash, hashcmp);
    if(p != NULL){
        auto proc = container_of(p, hashpid_t, node)->proc;
        if(is_unused(proc)) return -1;
        proc->killed = true;
        alert_proc(proc);
        _release_spinlock(&tree_lock);
        return 0;
    }
    _release_spinlock(&tree_lock);
    return -1;
}

int start_proc(struct proc* p, void(*entry)(u64), u64 arg)
{
    // TODO
    // 1. set the parent to root_proc if NULL
    // 2. setup the kcontext to make the proc start with proc_entry(entry, arg)
    // 3. activate the proc and return its pid
    // NOTE: be careful of concurrency
    if(p->parent == NULL){
        _acquire_spinlock(&tree_lock);
        p->parent = &root_proc;
        _insert_into_list(&root_proc.children, &p->ptnode);
        _release_spinlock(&tree_lock);
    }
    p->kcontext->lr = (u64)&proc_entry;
    p->kcontext->x0 = (u64)entry;
    p->kcontext->x1 = (u64)arg;
    int id = p->pid;
    activate_proc(p);
    return id;
}

void init_proc(struct proc* p)
{
    // TODO
    // setup the struct proc with kstack and pid allocated
    // NOTE: be careful of concurrency
    p->killed = false;
    p->idle = false;
    _acquire_spinlock(&tree_lock);
    p->pid = alloc_pidmap(p);
    _release_spinlock(&tree_lock);
    p->state = UNUSED;
    init_sem(&(p->childexit),0);
    init_list_node(&(p->children));
    init_list_node(&(p->ptnode));
    p->parent = NULL;
    init_schinfo(&(p->schinfo));
    init_pgdir(&p->pgdir);
    p->kstack = kalloc_page();
    memset(p->kstack, 0, PAGE_SIZE);
    p->ucontext = p->kstack + PAGE_SIZE - 16 - sizeof(UserContext);
    p->kcontext = p->kstack + PAGE_SIZE - 16 - sizeof(UserContext) - sizeof(KernelContext);
}

struct proc* create_proc()
{
    struct proc* p = kalloc(sizeof(struct proc));
    init_proc(p);
    return p;
}

define_init(root_proc)
{
    init_proc(&root_proc);
    root_proc.parent = &root_proc;
    start_proc(&root_proc, kernel_entry, 123456);
}


void yield() {
    // TODO: lab7 container
    // Give up cpu resources and switch to the scheduler
    // 1. get sched lock
    // 2. set process state and call scheduler
    // 3. don't forget to release your lock

}

void sleep(void* chan, SpinLock* lock) {
    // TODO: lab7 container
    // release lock and sleep on channel, reacquire lock when awakened
}

void wakeup(void* chan) {
    // TODO lab7 container
    // wake up all sleeping processses on channel
}
