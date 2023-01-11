#include<kernel/console.h>
#include<kernel/init.h>
#include<aarch64/intrinsic.h>
#include<kernel/sched.h>
#include<driver/uart.h>
#define INPUT_BUF 128
struct {
    char buf[INPUT_BUF];
    usize r;  // Read index
    usize w;  // Write index
    usize e;  // Edit index
} input;
#define C(x)      ((x) - '@')  // Control-x
#define BACKSPACE 0x100


isize console_write(Inode *ip, char *buf, isize n) {
    // TODO
    inodes.unlock(ip);
    for(isize i = 0; i < n; i++){
        uart_put_char(buf[i]);
    }
    inodes.lock(ip);
    return n;
}

isize console_read(Inode *ip, char *dst, isize n) {
    // TODO
    inodes.unlock(ip);
    isize i = 0;
    while(input.r != input.w && i != n && input.buf[input.r % INPUT_BUF] != C('D')){
        dst[i++] = input.buf[input.r++ % INPUT_BUF];
    }
    inodes.lock(ip);
    return i;
}

void console_intr(char (*getc)()) {
    // TODO
    char c;
    while(uart_valid_char(c = getc())){
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
                kill(thisproc()->pid);
                break;

            default:
                if(input.r + INPUT_BUF != input.e){
                    input.buf[input.e++ % INPUT_BUF] = c;
                    uart_put_char(c);
                    if(c == '\n' || c == C('D'))
                        input.w = input.e;
                }
                break;
        }
    }
}
