#pragma once

#define SYS_myreport 499
#define SYS_pstat 500
#define SYS_sbrk 12

#define SYS_clone 220
#define SYS_myexit 457
#define SYS_myyield 459

#define SYS_exit 93
#define SYS_set_tid_address 96
#define SYS_yield 124
#define SYS_sigprocmask 135
#define SYS_rt_sigprocmask 135
#define SYS_gettid 178
#define SYS_execve 221
#define SYS_sys_wait4 498

// find more in musl/arch/aarch64/bits/syscall.h