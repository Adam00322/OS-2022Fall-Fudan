#include <kernel/cpu.h>
#include <kernel/printk.h>
#include <kernel/init.h>
#include <kernel/sched.h>
#include <test/test.h>
#include <driver/sd.h>

bool panic_flag;

NO_RETURN void idle_entry() {
    set_cpu_on();
    while (1) {
        yield();
        if (panic_flag)
            break;
        arch_with_trap {
            arch_wfi();
        }
    }
    set_cpu_off();
    arch_stop_cpu();
}

extern void icode();
extern void eicode();
extern void trap_return();
void kernel_entry() {
    printk("hello world %d\n", (int)sizeof(struct proc));

    // proc_test();
    // user_proc_test();
    // container_test();
    // sd_test();
    
    do_rest_init();
    // pgfault_first_test();
    // pgfault_second_test();

    // TODO: map init.S to user space and trap_return to run icode
    auto p = thisproc();
    for(u64 ka = PAGE_BASE((u64)icode), va = 0x0; ka <= (u64)eicode; va += PAGE_SIZE, ka += PAGE_SIZE){
        vmmap(&p->pgdir, va, (void*)ka, PTE_USER_DATA | PTE_RO);
    }
    p->cwd = inodes.root;
    p->ucontext->elr = (u64)icode - PAGE_BASE((u64)icode);
    set_return_addr(trap_return);
}

NO_INLINE NO_RETURN void _panic(const char* file, int line) {
    printk("=====%s:%d PANIC%d!=====\n", file, line, cpuid());
    panic_flag = true;
    set_cpu_off();
    for (int i = 0; i < NCPU; i++) {
        if (cpus[i].online)
            i--;
    }
    printk("Kernel PANIC invoked at %s:%d. Stopped.\n", file, line);
    arch_stop_cpu();
}
