#include <kernel/sched.h>
#include <kernel/proc.h>
#include <kernel/init.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <aarch64/intrinsic.h>
#include <kernel/cpu.h>
#include <driver/clock.h>
#include <common/string.h>

extern bool panic_flag;

extern void swtch(KernelContext* new_ctx, KernelContext** old_ctx);

static SpinLock schelock;
extern struct container root_container;

static u64 starttime[NCPU];
static struct timer clock_interupt[NCPU];

define_early_init(schelock)
{
    init_spinlock(&schelock);
}

define_init(sched)
{
    for(int i=0; i<NCPU; i++){
        struct proc* p = kalloc(sizeof(struct proc));
        memset(p, 0, sizeof(struct proc));
        p->pid = -1;
        p->killed = false;
        p->idle = 1;
        p->state = RUNNING;
        cpus[i].sched.thisproc = cpus[i].sched.idle = p;
        starttime[i] = 0;
        p->schinfo.prio = 39;
        p->schinfo.weight = prio_to_weight[39];
        p->container = &root_container;
    }
}

inline static u64 min_vruntime(rb_root root){
    auto t = _rb_first(root);
    if(t == NULL) return thisproc()->schinfo.vruntime;
    return container_of(t, struct schinfo, node)->vruntime;
}

bool cmp(rb_node lnode,rb_node rnode){
    auto l = container_of(lnode, struct schinfo, node);
    auto r = container_of(rnode, struct schinfo, node);
    if(l->vruntime == r->vruntime) return l < r;
    else return l->vruntime < r->vruntime;
}


struct proc* thisproc()
{
    // TODO: return the current process
    return cpus[cpuid()].sched.thisproc;
}

void init_schinfo(struct schinfo* p, bool group)
{
    // TODO: initialize your customized schinfo for every newly-created process
    p->vruntime = 0;
    p->prio = 21;
    p->weight = prio_to_weight[p->prio];
    p->iscontainer = group;
}

void init_schqueue(struct schqueue* s)
{
    s->root.rb_node = NULL;
    s->weight_sum = 0;
    s->Running = false;
    s->sched_latency = 6;
}

void _acquire_sched_lock()
{
    // TODO: acquire the sched_lock if need
    _acquire_spinlock(&schelock);
}

void _release_sched_lock()
{
    // TODO: release the sched_lock if need
    _release_spinlock(&schelock);
}

bool is_zombie(struct proc* p)
{
    bool r;
    _acquire_sched_lock();
    r = p->state == ZOMBIE;
    _release_sched_lock();
    return r;
}

bool is_unused(struct proc* p)
{
    bool r;
    _acquire_sched_lock();
    r = p->state == UNUSED;
    _release_sched_lock();
    return r;
}

bool _activate_proc(struct proc* p, bool onalert)
{
    // TODO
    // if the proc->state is RUNNING/RUNNABLE, do nothing and return false
    // if the proc->state is SLEEPING/UNUSED, set the process state to RUNNABLE, add it to the sched queue, and return true
    // if the proc->state is DEEPSLEEING, do nothing if onalert or activate it if else, and return the corresponding value.
    _acquire_sched_lock();
    if(p->state == RUNNING || p->state == RUNNABLE || p->state == ZOMBIE || (p->state == DEEPSLEEPING && onalert)){
        _release_sched_lock();
        return false;
    }
    p->state = RUNNABLE;
    p->schinfo.vruntime = min_vruntime(&p->container->schqueue.root);
    p->container->schqueue.weight_sum += p->schinfo.weight;
    ASSERT(!_rb_insert(&p->schinfo.node, &p->container->schqueue.root, cmp));
    if(p->state == SLEEPING){
        auto container = p->container;
        while(!container->schqueue.Running && container != &root_container && !_rb_lookup(&container->schinfo.node, &container->parent->schqueue.root, cmp)){
            container->schinfo.vruntime = min_vruntime(&container->parent->schqueue.root);
            container->parent->schqueue.weight_sum += container->schinfo.weight;
            ASSERT(!_rb_insert(&container->schinfo.node, &container->parent->schqueue.root, cmp));
            container = container->parent;
        }
    }
    _release_sched_lock();
    return true;
}

void activate_group(struct container* group)
{
    // TODO: add the schinfo node of the group to the schqueue of its parent
    _acquire_sched_lock();
    group->schinfo.vruntime = min_vruntime(&group->parent->schqueue.root);
    group->parent->schqueue.weight_sum += group->schinfo.weight;
    ASSERT(!_rb_insert(&group->schinfo.node, &group->parent->schqueue.root, cmp));
    _release_sched_lock();
}

static void update_this_state(enum procstate new_state)
{
    // TODO: if using simple_sched, you should implement this routinue
    // update the state of current process to new_state, and remove it from the sched queue if new_state=SLEEPING/ZOMBIE
    auto p = thisproc();
    p->state = new_state;
    if(p->idle) return;
    u64 vruntime = (get_timestamp_ms() - starttime[cpuid()])*prio_to_weight[21]/p->schinfo.weight;
    p->schinfo.vruntime += vruntime;
    auto container = p->container;
    if(new_state == RUNNABLE){
        ASSERT(!_rb_insert(&p->schinfo.node, &p->container->schqueue.root, cmp));
        while(container != &root_container){
            container->schinfo.vruntime += vruntime;
            container->schqueue.Running = false;
            ASSERT(!_rb_insert(&container->schinfo.node, &container->parent->schqueue.root, cmp));
            container = container->parent;
        }
    }else{
        p->container->schqueue.weight_sum -= p->schinfo.weight;
        while(container != &root_container){
            container->schinfo.vruntime += vruntime;
            container->schqueue.Running = false;
            if(_rb_first(&container->schqueue.root) != NULL){
                ASSERT(!_rb_insert(&container->schinfo.node, &container->parent->schqueue.root, cmp));
            }
            container->parent->schqueue.weight_sum -= container->schinfo.weight;
            container = container->parent;
        }
    }
}

static struct proc* pick_next()
{
    // TODO: if using simple_sched, you should implement this routinue
    // choose the next process to run, and return idle if no runnable process
    auto container = &root_container;
    auto node = _rb_first(&container->schqueue.root);
    if(node != NULL){
        auto schinfo = container_of(node, struct schinfo, node);
        while(schinfo->iscontainer){
            container = container_of(schinfo, struct container, schinfo);
            container->schqueue.Running = true;
            container->schqueue.sched_latency = container->parent->schqueue.sched_latency * container->schinfo.weight / container->parent->schqueue.weight_sum;
            _rb_erase(&container->schinfo.node, &container->parent->schqueue.root);
            node = _rb_first(&container->schqueue.root);
            schinfo = container_of(node, struct schinfo, node);
        }
        auto proc = container_of(schinfo, struct proc, schinfo);
        _rb_erase(&proc->schinfo.node, &proc->container->schqueue.root);
        return proc;
    }
    return cpus[cpuid()].sched.idle;
}

void HandleClock(){
    clock_interupt[cpuid()].data--;
    setup_checker(qaq);
    lock_for_sched(qaq);
    sched(qaq, RUNNABLE);
}

static void update_this_proc(struct proc* p)
{
    // TODO: if using simple_sched, you should implement this routinue
    // update thisproc to the choosen process, and reset the clock interrupt if need
    while(clock_interupt[cpuid()].data){
        clock_interupt[cpuid()].data--;
        cancel_cpu_timer(&clock_interupt[cpuid()]);
    }
    clock_interupt[cpuid()].data++;
    if(p->idle) clock_interupt[cpuid()].elapse = 1;
    else clock_interupt[cpuid()].elapse = MAX(p->container->schqueue.sched_latency * p->schinfo.weight / p->container->schqueue.weight_sum, min_lantency);
    clock_interupt[cpuid()].handler = HandleClock;
    starttime[cpuid()] = get_timestamp_ms();
    set_cpu_timer(&clock_interupt[cpuid()]);
    cpus[cpuid()].sched.thisproc = p;
}

// A simple scheduler.
// You are allowed to replace it with whatever you like.
static void simple_sched(enum procstate new_state)
{
    auto this = thisproc();
    ASSERT(this->state == RUNNING);
    if(this->killed && new_state != ZOMBIE){
        _release_sched_lock();
        return;
    }
    update_this_state(new_state);
    auto next = pick_next();
    update_this_proc(next);
    ASSERT(next->state == RUNNABLE);
    next->state = RUNNING;
    if (next != this)
    {
        attach_pgdir(&next->pgdir);
        swtch(next->kcontext, &this->kcontext);
    }
    _release_sched_lock();
}

__attribute__((weak, alias("simple_sched"))) void _sched(enum procstate new_state);

u64 proc_entry(void(*entry)(u64), u64 arg)
{
    _release_sched_lock();
    set_return_addr(entry);
    return arg;
}

