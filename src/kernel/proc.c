#include <kernel/proc.h>
#include <kernel/init.h>
#include <kernel/mem.h>
#include <kernel/sched.h>
#include <common/list.h>
#include <common/string.h>
#include <kernel/printk.h>
#include <kernel/paging.h>

struct proc root_proc;
extern struct container root_container;

void kernel_entry();
void proc_entry();

static SpinLock tree_lock;
define_early_init(init_lock){
    init_spinlock(&tree_lock);
}

static struct hash_map_ h;
define_early_init(init_hash){
    _hashmap_init(&h);
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

pidmap_t globalpidmap;
define_early_init(init_globalpidmap){
    memset(globalpidmap.bitmap, 0, MAX_CONTAINER_PID / 8);
    globalpidmap.last_pid = -1;
}

static int alloc_pid(pidmap_t* pidmap)
{
    BitmapCell* bitmap = pidmap->bitmap;
    int pid = pidmap->last_pid + 1;
    while (pid < MAX_CONTAINER_PID && bitmap_get(bitmap, pid)){
        ++pid;
    }
    if(pid == MAX_CONTAINER_PID){
        pid = 0;
        while (pid <= pidmap->last_pid && bitmap_get(bitmap, pid)){
            ++pid;
        }
    }
    if (MAX_CONTAINER_PID != pid && !bitmap_get(bitmap, pid)){
        pidmap->last_pid = pid;
        bitmap_set(bitmap, pid);
        return pid;
    }
    return -1;
}

static INLINE void free_pid(int pid, pidmap_t* pidmap)
{
    bitmap_clear(pidmap->bitmap, pid);
}

static void alloc_hashpid(struct proc* p)
{
    hashpid_t* hashpid = kalloc(sizeof(hashpid_t));
    hashpid->pid = p->pid;
    hashpid->proc = p;
    _hashmap_insert(&hashpid->node, &h, hash);
}

static void free_hashpid(int pid){
    auto hashnode = _hashmap_lookup(&(hashpid_t){pid, NULL, {NULL}}.node, &h, hash, hashcmp);
    _hashmap_erase(hashnode, &h, hash);
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
    // 3. transfer children to the rootproc of the container, and notify the it if there is zombie
    // 4. notify the parent
    // 5. sched(ZOMBIE)
    // NOTE: be careful of concurrency
    setup_checker(qwq);
    auto this = thisproc();
    ASSERT(this != this->container->rootproc && !this->idle);
    this->exitcode = code;
    free_pgdir(&this->pgdir);
    //TODO clean up file resources
    struct proc* rootproc = this->container->rootproc;
    _acquire_spinlock(&tree_lock);
    ListNode* pre = NULL;
    _for_in_list(p, &this->children){
        if(pre != NULL && pre != &this->children){
            auto proc = container_of(pre, struct proc, ptnode);
            proc->parent = rootproc;
            auto t = &rootproc->children;
            if(is_zombie(proc)){
                pre->prev = t->prev;
                pre->next = t;
                t->prev->next = pre;
                t->prev = pre;
                post_sem(&rootproc->childexit);
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

int wait(int* exitcode, int* pid)
{
    // TODO
    // 1. return -1 if no children
    // 2. wait for childexit
    // 3. if any child exits, clean it up and return its local pid and exitcode
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
        *pid = proc->pid;
        free_pid(proc->pid, &globalpidmap);
        free_hashpid(proc->pid);
        int localpid = proc->localpid;
        free_pid(localpid, &proc->container->pidmap);
        _detach_from_list(p);
        kfree_page(proc->kstack);
        kfree(proc);
        _release_spinlock(&tree_lock);
        return localpid;
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
    auto p = _hashmap_lookup(&(hashpid_t){pid, NULL, {NULL}}.node, &h, hash, hashcmp);
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
    // 3. activate the proc and return its local pid
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
    _acquire_spinlock(&p->container->pidlock);
    p->localpid = alloc_pid(&p->container->pidmap);
    _release_spinlock(&p->container->pidlock);
    activate_proc(p);
    return p->localpid;
}

void init_proc(struct proc* p)
{
    // TODO
    // setup the struct proc with kstack and pid allocated
    // NOTE: be careful of concurrency
    p->killed = false;
    p->idle = false;
    _acquire_spinlock(&tree_lock);
    p->pid = alloc_pid(&globalpidmap);
    alloc_hashpid(p);
    _release_spinlock(&tree_lock);
    p->state = UNUSED;
    init_sem(&(p->childexit),0);
    init_list_node(&(p->children));
    init_list_node(&(p->ptnode));
    p->parent = NULL;
    init_schinfo(&(p->schinfo), 0);
    init_pgdir(&p->pgdir);
    p->container = &root_container;
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

static struct proc* offline_proc;
static void find_offline_proc(struct proc* fa){
    if(offline_proc != NULL) return;
    _for_in_list(p, &fa->children){
        if(p == &fa->children) continue;
        auto proc = container_of(p, struct proc, ptnode);
        _acquire_spinlock(&proc->pgdir.lock);
        if(proc->state != ZOMBIE && proc->pgdir.online == false){
            offline_proc = proc;
            return ;
        }
        _release_spinlock(&proc->pgdir.lock);
        find_offline_proc(proc);
    }
}

struct proc* get_offline_proc(){
    _acquire_spinlock(&tree_lock);
    offline_proc = NULL;
    find_offline_proc(&root_proc);
    auto proc = offline_proc;
    _release_spinlock(&tree_lock);
    return proc;
}

extern void icode();
extern void eicode();
define_init(root_proc)
{
    init_proc(&root_proc);
    root_proc.parent = &root_proc;
    struct section* s = kalloc(sizeof(struct section));
    s->flags = ST_TEXT;
    s->length = (u64)eicode - PAGE_BASE((u64)icode);
    s->begin = 0x0;
    s->end = s->begin + s->length;
    _insert_into_list(&root_proc.pgdir.section_head, &s->stnode);
    start_proc(&root_proc, kernel_entry, 123456);
}

/*
 * Create a new process copying p as the parent.
 * Sets up stack to return as if from system call.
 */
void trap_return();
int fork() {
    /* TODO: Your code here. */
    auto np = create_proc();
    auto p = thisproc();

    copy_sections(&p->pgdir.section_head, &np->pgdir.section_head);
    PTEntriesPtr pte;
    u64 va = 0;
    while((pte = get_pte(&p->pgdir, va, false)) != NULL && (*pte & PTE_VALID)){
        vmmap(&np->pgdir, va, (void*)P2K(PTE_ADDRESS(*pte)), PTE_FLAGS(*pte) | PTE_RO);
        va += PAGE_SIZE;
    }

    *np->ucontext = *p->ucontext;
    np->ucontext->x[0] = 0;

    for(int i=0; i<NOFILE; i++){
        if(p->oftable.fp[i])
            np->oftable.fp[i] = filedup(p->oftable.fp[i]);
    }
    np->cwd = inodes.share(p->cwd);

    set_parent_to_this(np);
    start_proc(np, trap_return, 0);
    return np->pid;
}