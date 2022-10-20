#pragma once

#include <common/list.h>
#include <common/rbtree.h>

#define sched_latency 20
#define min_lantency 1

struct proc; // dont include proc.h here

// embedded data for cpus
struct sched
{
    // TODO: customize your sched info
    struct proc* thisproc;
    struct proc* idle;
};

// embeded data for procs
struct schinfo
{
    // TODO: customize your sched info
    u64 vruntime;
    int prio;
    int weight;
    struct rb_node_ node;
    // ListNode rq;
};

static const int prio_to_weight[40]={
/* -20 */ 88761, 71755, 56483, 46273, 36291,
/* -15 */ 29154, 23254, 18705, 14949, 11916,
/* -10 */ 9548, 7620, 6100, 4904, 3906,
/* -5 */ 3121, 2501, 1991, 1586, 1277,
/* 0 */ 1024, 820, 655, 526, 423,
/* 5 */ 335, 272, 215, 172, 137,
/* 10 */ 110, 87, 70, 56, 45,
/* 15 */ 36, 29, 23, 18, 15
};