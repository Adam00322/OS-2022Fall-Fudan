/* File descriptors */

#include "file.h"
#include <common/defines.h>
#include <common/spinlock.h>
#include <common/sem.h>
#include <fs/inode.h>
#include <common/list.h>
#include <kernel/mem.h>
#include "fs.h"
#include "pipe.h"

static struct ftable ftable;

void init_ftable() {
    // TODO: initialize your ftable
    init_spinlock(&ftable.lock);
}

void init_oftable(struct oftable *oftable) {
    // TODO: initialize your oftable for a new process
    for(int i=0; i<NOFILE; i++){
        oftable->fp[i] = NULL;
    }
}

/* Allocate a file structure. */
struct file* filealloc() {
    /* TODO: Lab10 Shell */
    _acquire_spinlock(&ftable.lock);
    for(int i=0; i<NOFILE; i++){
        if(ftable.file[i].ref <= 0){
            ftable.file[i].ref = 1;
            _release_spinlock(&ftable.lock);
            return &ftable.file[i];
        }
    }
    _release_spinlock(&ftable.lock);
    return NULL;
}

/* Increment ref count for file f. */
struct file* filedup(struct file* f) {
    /* TODO: Lab10 Shell */
    _acquire_spinlock(&ftable.lock);
    f->ref++;
    _release_spinlock(&ftable.lock);
    return f;
}

/* Close file f. (Decrement ref count, close when reaches 0.) */
void fileclose(struct file* f) {
    /* TODO: Lab10 Shell */
    _acquire_spinlock(&ftable.lock);
    f->ref--;
    if(f->ref == 0){
        if(f->type == FD_INODE){
            Inode* inode = f->ip;
            f->type = FD_NONE;
            _release_spinlock(&ftable.lock);
            OpContext ctx;
            bcache.begin_op(&ctx);
            inodes.put(&ctx, inode);
            bcache.end_op(&ctx);
        }else if(f->type == FD_PIPE){
            pipeClose(f->pipe, f->writable);
            _release_spinlock(&ftable.lock);
        }
        return;
    }
    _release_spinlock(&ftable.lock);
}

/* Get metadata about file f. */
int filestat(struct file* f, struct stat* st) {
    /* TODO: Lab10 Shell */
    if(f->type == FD_INODE){
        inodes.lock(f->ip);
        stati(f->ip, st);
        inodes.unlock(f->ip);
        return 0;
    }
    return -1;
}

/* Read from file f. */
isize fileread(struct file* f, char* addr, isize n) {
    /* TODO: Lab10 Shell */
    if(f->type == FD_INODE && f->readable){
        inodes.lock(f->ip);
        n = inodes.read(f->ip, (u8*)addr, f->off, n);
        f->off += n;
        inodes.unlock(f->ip);
        return n;
    }else if(f->type == FD_PIPE && f->readable){
        return pipeRead(f->pipe, (u64)addr, n);
    }
    return -1;
}

/* Write to file f. */
isize filewrite(struct file* f, char* addr, isize n) {
    /* TODO: Lab10 Shell */
    if(f->type == FD_INODE && f->writable){
        usize mx = (OP_MAX_NUM_BLOCKS-1-1-2) / 2 * BLOCK_SIZE;
        isize t = 0;
        while(t < n){
            t = MIN(mx, (usize)n-t);
            OpContext ctx;
            bcache.begin_op(&ctx);
            inodes.lock(f->ip);
            t = inodes.write(&ctx, f->ip, (u8*)addr, f->off, t);
            f->off += t;
            inodes.unlock(f->ip);
            bcache.end_op(&ctx);
        }
        return t;
    }else if(f->type == FD_PIPE && f->writable){
        return pipeWrite(f->pipe, (u64)addr, n);
    }
    return -1;
}
