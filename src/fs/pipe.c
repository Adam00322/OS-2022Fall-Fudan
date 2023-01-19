#include <kernel/mem.h>
#include <kernel/sched.h>
#include <fs/pipe.h>
#include <common/string.h>

int pipeAlloc(File** f0, File** f1) {
    // TODO
    (*f0) = filealloc();
    if(*f0 == NULL) return -1;
    (*f1) = filealloc();
    if(*f1 == NULL){
        fileclose(*f0);
        return -1;
    }

    Pipe* pi = kalloc(sizeof(Pipe));
    init_spinlock(&pi->lock);
    init_sem(&pi->wlock, 0);
    init_sem(&pi->rlock, 0);
    pi->nread = pi->nwrite = 0;
    pi->readopen = pi->writeopen = 1;
    
    (*f0)->type = (*f1)->type = FD_PIPE;
    (*f0)->readable = (*f1)->writable = 1;
    (*f0)->writable = (*f1)->readable = 0;
    (*f0)->pipe = (*f1)->pipe = pi;
    
    return 0;
}

void pipeClose(Pipe* pi, int writable) {
    // TODO
    _acquire_spinlock(&pi->lock);
    if(writable){
        pi->writeopen = 0;
        post_all_sem(&pi->rlock);
    }else{
        pi->readopen = 0;
        post_all_sem(&pi->wlock);
    }

    if(pi->readopen == 0 && pi->writeopen == 0){
        kfree(pi);
        return;
    }
    _release_spinlock(&pi->lock);
}

int pipeWrite(Pipe* pi, u64 addr, int n) {
    // TODO
    if(!pi->writeopen) return 0;
    char* src = (char*)addr;
    int i = 0;
    _acquire_spinlock(&pi->lock);
    while(i < n){
        while(pi->nread + PIPESIZE == pi->nwrite){
            if(pi->readopen == 0){
                _release_spinlock(&pi->lock);
                return i;
            }

            _lock_sem(&pi->wlock);
            _release_spinlock(&pi->lock);
            post_all_sem(&pi->rlock);
            if(_wait_sem(&pi->wlock, true) == 0) return i;
            _acquire_spinlock(&pi->lock);
        }
        pi->data[pi->nwrite++ % PIPESIZE] = src[i++];
    }
    _release_spinlock(&pi->lock);
    post_all_sem(&pi->rlock);
    return i;
}

int pipeRead(Pipe* pi, u64 addr, int n) {
    // TODO
    if(!pi->readopen) return 0;
    char* dst = (char*)addr;
    int i = 0;
    _acquire_spinlock(&pi->lock);
    while(i < n){
        while(pi->nread == pi->nwrite){
            if(pi->writeopen == 0){
                _release_spinlock(&pi->lock);
                return i;
            }

            _lock_sem(&pi->rlock);
            _release_spinlock(&pi->lock);
            post_all_sem(&pi->wlock);
            if(_wait_sem(&pi->rlock, true) == 0) return i;
            _acquire_spinlock(&pi->lock);
        }
        dst[i++] = pi->data[pi->nread++ % PIPESIZE];
        if(dst[i-1] == '\n') break;
    }
    _release_spinlock(&pi->lock);
    post_all_sem(&pi->wlock);
    return i;
}