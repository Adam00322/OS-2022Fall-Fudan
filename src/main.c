#include <aarch64/intrinsic.h>
#include <kernel/init.h>
#include <driver/uart.h>
#include <common/string.h>

static char hello[16];
extern char edata[], end[];

define_early_init(fill){
    strncpy_fast(hello, "Hello world!", sizeof("Hello world!"));
}

define_init(out){
    int i = 0;
    while(hello[i]!='\0'){
        uart_put_char(hello[i]);
        i++;
    }
}

NO_RETURN void main()
{
    if(cpuid()!=0) arch_stop_cpu();
    for (char* p = edata; p < end; p++){
        *p = 0;
    }
    do_early_init();
    do_init();
    arch_stop_cpu();
}
