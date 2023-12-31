#include <kernel/syscall.h>
#include <kernel/sched.h>
#include <kernel/printk.h>
#include <common/sem.h>
#include <kernel/pt.h>
#include <kernel/paging.h>

void* syscall_table[NR_SYSCALL];

void syscall_entry(UserContext* context)
{
    // TODO
    // Invoke syscall_table[id] with args and set the return value.
    // id is stored in x8. args are stored in x0-x5. return value is stored in x0.
    u64 id = 0, ret = 0;
    id = context->x[8];
    if (id < NR_SYSCALL)
    {
        u64 (*p) (u64, u64, u64, u64, u64, u64) = syscall_table[id];
        if(p == NULL) PANIC();
        ret = p(context->x[0], context->x[1], context->x[2], context->x[3], context->x[4], context->x[5]);
        context->x[0] = ret;
    }
}

// check if the virtual address [start,start+size) is READABLE by the current user process
bool user_readable(const void* start, usize size) {
    // TODO
    struct section* s = NULL;
    u64 addr = (u64)start;
    auto pd = &thisproc()->pgdir;
	_for_in_list(p, &pd->section_head){
		if(p == &pd->section_head) continue;
		auto st = container_of(p, struct section, stnode);
		if(addr >= st->begin && addr+size-1 < st->end){
            s = st;
			break;
        }
	}
    if(s == NULL) return false;
    return true;
}

// check if the virtual address [start,start+size) is READABLE & WRITEABLE by the current user process
bool user_writeable(const void* start, usize size) {
    // TODO
    struct section* s = NULL;
    u64 addr = (u64)start;
    auto pd = &thisproc()->pgdir;
	_for_in_list(p, &pd->section_head){
		if(p == &pd->section_head) continue;
		auto st = container_of(p, struct section, stnode);
		if(addr >= st->begin && addr+size-1 < st->end){
            s = st;
			break;
        }
	}
    if(s == NULL || (s->flags&ST_RO)) return false;
    return true;
}

// get the length of a string including tailing '\0' in the memory space of current user process
// return 0 if the length exceeds maxlen or the string is not readable by the current user process
usize user_strlen(const char* str, usize maxlen) {
    for (usize i = 0; i < maxlen; i++) {
        if (user_readable(&str[i], 1)) {
            if (str[i] == 0)
                return i + 1;
        } else
            return 0;
    }
    return 0;
}
