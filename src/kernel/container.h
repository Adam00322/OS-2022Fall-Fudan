#pragma once

#include <kernel/proc.h>
#include <kernel/schinfo.h>
#include <common/bitmap.h>

#define MAX_CONTAINER_PID 256

typedef struct pidmap
{
    Bitmap(bitmap, MAX_CONTAINER_PID);
    int last_pid;
} pidmap_t;


struct container
{
    struct container* parent;
    struct proc* rootproc;

    struct schinfo schinfo;
    struct schqueue schqueue;

    // TODO: namespace (local pid?)
    pidmap_t pidmap;
    SpinLock pidlock;
};

struct container* create_container(void (*root_entry)(), u64 arg);
void set_container_to_this(struct proc*);
