#include<kernel/console.h>
#include<kernel/init.h>
#include<aarch64/intrinsic.h>
#include<kernel/sched.h>
#include<driver/uart.h>
#include<driver/interrupt.h>
#define INPUT_BUF 128
struct proc* shell, *shellchild;
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
    shell = thisproc();
    isize i = 0;
    _acquire_spinlock(&input.lock);
    while(i != n){
        while(input.r == input.w){
            _lock_sem(&input.read);
            _release_spinlock(&input.lock);
            if(_wait_sem(&input.read, true) == false){
                inodes.lock(ip);
                return -1;
            }
            _acquire_spinlock(&input.lock);
        }
        if(input.buf[input.r % INPUT_BUF] == C('D')) break;
        dst[i++] = input.buf[input.r++ % INPUT_BUF];
        if(dst[i-1] == '\n') break;
    }
    if(i == 0 && input.buf[input.r % INPUT_BUF] == C('D')) input.r++;
    _release_spinlock(&input.lock);
    inodes.lock(ip);
    return i;
}

void console_intr() {
    // TODO
    char c;
    _acquire_spinlock(&input.lock);
    while((c = uart_get_char()) != 0xff){
        switch(c){
            case 127:
            case '\b':
                if(input.e != input.w){
                    input.e--;
                    uart_put_char('\b');
                    uart_put_char(' ');
                    uart_put_char('\b');
                    break;
                }

            case C('U'):
                while(input.e != input.w){
                    input.e--;
                    uart_put_char('\b');
                    uart_put_char(' ');
                    uart_put_char('\b');
                }
                break;
            
            case C('C'):
                if(!thisproc()->idle){
                    ASSERT(kill(thisproc()->pid) != -1);
                }
                break;

            default:
                if(input.r + INPUT_BUF != input.e){
                    if(c == '\r') c = '\n';
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
