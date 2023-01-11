#include<kernel/console.h>
#include<kernel/init.h>
#include<aarch64/intrinsic.h>
#include<kernel/sched.h>
#include<driver/uart.h>
#include<driver/interrupt.h>
#define INPUT_BUF 128
struct {
    char buf[INPUT_BUF];
    usize r;  // Read index
    usize w;  // Write index
    usize e;  // Edit index
    SpinLock lock;
    Semaphore read;
} input;
#define C(x)      ((x) - '@')  // Control-x

define_rest_init(console){
    set_interrupt_handler(IRQ_AUX, console_intr);
    init_spinlock(&input.lock);
    init_sem(&input.read, 0);
}

isize console_write(Inode *ip, char *buf, isize n) {
    // TODO
    ASSERT(ip->entry.type == INODE_DEVICE);
    inodes.unlock(ip);
    _acquire_spinlock(&input.lock);
    for(isize i = 0; i < n; i++){
        if(buf[i] == '\b'){
            uart_put_char('\b');
            uart_put_char(' ');
        }
        uart_put_char(buf[i]);
    }
    _release_spinlock(&input.lock);
    inodes.lock(ip);
    return n;
}

isize console_read(Inode *ip, char *dst, isize n) {
    // TODO
    ASSERT(ip->entry.type == INODE_DEVICE);
    inodes.unlock(ip);
    isize i = 0;
    _acquire_spinlock(&input.lock);
    while(i != n && input.buf[input.r % INPUT_BUF] != C('D')){
        while(input.r == input.w){
            _lock_sem(&input.read);
            _release_spinlock(&input.lock);
            if(_wait_sem(&input.read, true) == false){
                inodes.lock(ip);
                return -1;
            }
            _acquire_spinlock(&input.lock);
        }
        dst[i++] = input.buf[input.r++ % INPUT_BUF];
    }
    _release_spinlock(&input.lock);
    inodes.lock(ip);
    return i;
}

void console_intr() {
    // TODO
    char c;
    _acquire_spinlock(&input.lock);
    while(uart_valid_char(c = uart_get_char())){
        switch(c){
            case '\b':
                input.e--;
                uart_put_char('\b');
                uart_put_char(' ');
                uart_put_char('\b');
                break;

            case C('U'):
                while(input.e-- != input.w){
                    uart_put_char('\b');
                    uart_put_char(' ');
                    uart_put_char('\b');
                }
                break;
            
            case C('C'):
                ASSERT(kill(thisproc()->pid) != -1);
                break;

            default:
                if(input.r + INPUT_BUF != input.e){
                    input.buf[input.e++ % INPUT_BUF] = c;
                    uart_put_char(c);
                    if(c == '\n' || c == C('D')){
                        input.w = input.e;
                        post_all_sem(&input.read);
                    }
                }
                break;
        }
    }
    _release_spinlock(&input.lock);
}
