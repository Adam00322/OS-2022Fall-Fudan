//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include <fcntl.h>

#include <aarch64/mmu.h>
#include <common/defines.h>
#include <common/spinlock.h>
#include <kernel/printk.h>
#include <kernel/proc.h>
#include <kernel/sched.h>
#include <kernel/printk.h>
#include <kernel/mem.h>
#include <kernel/paging.h>
#include <fs/file.h>
#include <fs/fs.h>
#include <sys/syscall.h>
#include <kernel/mem.h>
#include "syscall.h"
#include <fs/pipe.h>
#include <common/string.h>
#include <fs/inode.h>

struct iovec {
    void* iov_base; /* Starting address. */
    usize iov_len; /* Number of bytes to transfer. */
};


// get the file object by fd
// return null if the fd is invalid
static struct file* fd2file(int fd) {
    // TODO
    if(fd >= NOFILE || fd < 0)
        return NULL;
    return thisproc()->oftable.fp[fd];
}

/*
 * Allocate a file descriptor for the given file.
 * Takes over file reference from caller on success.
 */
int fdalloc(struct file* f) {
    /* TODO: Lab10 Shell */
    auto oftable = &thisproc()->oftable;
    for(int i=0; i<NFILE; i++){
        if(oftable->fp[i] == NULL){
            oftable->fp[i] = f;
            return i;
        }
    }
    return -1;
}

define_syscall(ioctl, int fd, u64 request) {
    ASSERT(request == 0x5413);
    (void)fd;
    return 0;
}

/*
 *	map addr to a file
 */
define_syscall(mmap, void* addr, int length, int prot, int flags, int fd, int offset) {
    // TODO
    if(!fd2file(fd) || fd2file(fd)->type != FD_INODE) return -1;
    struct section* st = kalloc(sizeof(struct section));
    auto pd = &thisproc()->pgdir;
    if(!addr){
        u64 begin = 0;
        _for_in_list(p, &pd->section_head){
            if(p == &pd->section_head) continue;
            auto s = container_of(p, struct section, stnode);
            begin = MAX(begin, s->end);
        }
        addr = (void*)PAGE_BASE(begin + (PAGE_SIZE<<4));
    }
    st->length = length;
    st->begin = (u64)addr;
    st->end = st->begin + st->length;
    if(prot & PROT_WRITE) st->flags = ST_FILE;
    else st->flags = ST_FILE | ST_RO;
    st->fp = fd2file(fd);
    st->offset = offset;
    init_sleeplock(&st->sleeplock);
    _insert_into_list(&pd->section_head, &st->stnode);

    flags = flags;
    return 0;
}

define_syscall(munmap, void *addr, int length) {
    // TODO
    struct section* st = NULL;
    auto pd = &thisproc()->pgdir;
    _for_in_list(p, &pd->section_head){
        if(p == &pd->section_head) continue;
        auto s = container_of(p, struct section, stnode);
        if((u64)addr >= s->begin && (u64)addr + length < s->end){
			st = s;
			break;
		}
    }
    if(!st) return -1;
    OpContext ctx;
    bcache.begin_op(&ctx);
    inodes.write(&ctx, st->fp->ip, addr, st->offset + (u64)addr - st->begin, length);
    bcache.end_op(&ctx);
    _detach_from_list(&st->stnode);
    if((u64)addr > st->begin){
        struct section* ns = kalloc(sizeof(struct section));
        *ns = *st;
        ns->end = (u64)addr;
        ns->length = ns->end - ns->begin;
        _insert_into_list(&pd->section_head, &ns->stnode);
    }
    if((u64)addr + length < st->end){
        struct section* ns = kalloc(sizeof(struct section));
        *ns = *st;
        ns->begin = (u64)addr + length;
        ns->length = ns->end - ns->begin;
        ns->offset += ns->begin - st->begin;
        _insert_into_list(&pd->section_head, &ns->stnode);
    }
    kfree(st);
    return 0;
}

/*
 * Get the parameters and call filedup.
 */
define_syscall(dup, int fd) {
    struct file* f = fd2file(fd);
    if (!f)
        return -1;
    int nfd = fdalloc(f);
    if (nfd < 0)
        return -1;
    filedup(f);
    return nfd;
}

/*
 * Get the parameters and call fileread.
 */
define_syscall(read, int fd, char* buffer, int size) {
    struct file* f = fd2file(fd);
    if (!f || size <= 0 || !user_writeable(buffer, size))
        return -1;
    char* ka = alloc_page_for_user();
    int i = 0;
    while(i < size){
        int t = fileread(f, ka, MIN(PAGE_SIZE, size - i));
        memcpy(buffer + i, ka, MAX(t, 0));
        i += t;
        if(t != MIN(PAGE_SIZE, size - i)) break;
    }
    kfree_page(ka);
    return i;
}

/*
 * Get the parameters and call filewrite.
 */
define_syscall(write, int fd, char* buffer, int size) {
    struct file* f = fd2file(fd);
    if (!f || size <= 0 || !user_readable(buffer, size))
        return -1;
    char* ka = alloc_page_for_user();
    int i = 0;
    while(i < size){
        int t = MIN(PAGE_SIZE, size - i);
        memcpy(ka, buffer + i, t);
        t = filewrite(f, ka, t);
        i += t;
        if(t != MIN(PAGE_SIZE, size - i)) break;
    }
    kfree_page(ka);
    return i;
}

define_syscall(writev, int fd, struct iovec *iov, int iovcnt) {
    struct file* f = fd2file(fd);
    struct iovec *p;
    if (!f || iovcnt <= 0 || !user_readable(iov, sizeof(struct iovec) * iovcnt))
        return -1;
    usize tot = 0;
    char* ka = alloc_page_for_user();
    for (p = iov; p < iov + iovcnt; p++) {
        if (!user_readable(p->iov_base, p->iov_len))
            return -1;
        int i = 0;
        while(i < (int)p->iov_len){
            int t = MIN(PAGE_SIZE, (int)p->iov_len - i);
            memcpy(ka, p->iov_base + i, t);
            t = filewrite(f, ka, t);
            i += t;
            if(t != MIN(PAGE_SIZE, (int)p->iov_len - i)) break;
        }
        tot += i;
    }
    kfree_page(ka);
    return tot;
}

/*
 * Get the parameters and call fileclose.
 * Clear this fd of this process.
 */
define_syscall(close, int fd) {
    /* TODO: Lab10 Shell */
    if(fd >= NOFILE || fd < 0)
        return -1;
    fileclose(fd2file(fd));
    thisproc()->oftable.fp[fd] = NULL;
    return 0;
}

/*
 * Get the parameters and call filestat.
 */
define_syscall(fstat, int fd, struct stat* st) {
    struct file* f = fd2file(fd);
    if (!f || !user_writeable(st, sizeof(*st)))
        return -1;
    return filestat(f, st);
}

define_syscall(newfstatat, int dirfd, const char* path, struct stat* st, int flags) {
    if (!user_strlen(path, 256) || !user_writeable(st, sizeof(*st)))
        return -1;
    if (dirfd != AT_FDCWD) {
        printk("sys_fstatat: dirfd unimplemented\n");
        return -1;
    }
    if (flags != 0) {
        printk("sys_fstatat: flags unimplemented\n");
        return -1;
    }

    Inode* ip;
    OpContext ctx;
    bcache.begin_op(&ctx);
    if ((ip = namei(path, &ctx)) == 0) {
        bcache.end_op(&ctx);
        return -1;
    }
    inodes.lock(ip);
    stati(ip, st);
    inodes.unlock(ip);
    inodes.put(&ctx, ip);
    bcache.end_op(&ctx);

    return 0;
}

// Is the directory dp empty except for "." and ".." ?
static int isdirempty(Inode* dp) {
    usize off;
    DirEntry de;

    for (off = 2 * sizeof(de); off < dp->entry.num_bytes; off += sizeof(de)) {
        if (inodes.read(dp, (u8*)&de, off, sizeof(de)) != sizeof(de))
            PANIC();
        if (de.inode_no != 0)
            return 0;
    }
    return 1;
}

define_syscall(unlinkat, int fd, const char* path, int flag) {
    ASSERT(fd == AT_FDCWD && flag == 0);
    Inode *ip, *dp;
    DirEntry de;
    char name[FILE_NAME_MAX_LENGTH];
    usize off;
    if (!user_strlen(path, 256))
        return -1;
    OpContext ctx;
    bcache.begin_op(&ctx);
    if ((dp = nameiparent(path, name, &ctx)) == 0) {
        bcache.end_op(&ctx);
        return -1;
    }

    inodes.lock(dp);

    // Cannot unlink "." or "..".
    if (strncmp(name, ".", FILE_NAME_MAX_LENGTH) == 0
        || strncmp(name, "..", FILE_NAME_MAX_LENGTH) == 0)
        goto bad;

    usize inumber = inodes.lookup(dp, name, &off);
    if (inumber == 0)
        goto bad;
    ip = inodes.get(inumber);
    inodes.lock(ip);

    if (ip->entry.num_links < 1)
        PANIC();
    if (ip->entry.type == INODE_DIRECTORY && !isdirempty(ip)) {
        inodes.unlock(ip);
        inodes.put(&ctx, ip);
        goto bad;
    }

    memset(&de, 0, sizeof(de));
    if (inodes.write(&ctx, dp, (u8*)&de, off, sizeof(de)) != sizeof(de))
        PANIC();
    if (ip->entry.type == INODE_DIRECTORY) {
        dp->entry.num_links--;
        inodes.sync(&ctx, dp, true);
    }
    inodes.unlock(dp);
    inodes.put(&ctx, dp);
    ip->entry.num_links--;
    inodes.sync(&ctx, ip, true);
    inodes.unlock(ip);
    inodes.put(&ctx, ip);
    bcache.end_op(&ctx);
    return 0;

bad:
    inodes.unlock(dp);
    inodes.put(&ctx, dp);
    bcache.end_op(&ctx);
    return -1;
}
/*
 * Create an inode.
 *
 * Example:
 * Path is "/foo/bar/bar1", type is normal file.
 * You should get the inode of "/foo/bar", and
 * create an inode named "bar1" in this directory.
 *
 * If type is directory, you should additionally handle "." and "..".
 */
Inode* create(const char* path, short type, short major, short minor, OpContext* ctx) {
    /* TODO: Lab10 Shell */
    char name[FILE_NAME_MAX_LENGTH];
    Inode* parentinode = nameiparent(path, name, ctx);
    if(parentinode == NULL)
        return NULL;

    usize inode_no;
    Inode* inode;
    inodes.lock(parentinode);
    if((inode_no = inodes.lookup(parentinode, name, NULL)) != 0){
        inodes.unlock(parentinode);
        inodes.put(ctx, parentinode);
        inode = inodes.get(inode_no);
        inodes.lock(inode);
        if(type == inode->entry.type)
            return inode;
        inodes.unlock(inode);
        inodes.put(ctx, inode);
        return NULL;
    }
    inode_no = inodes.alloc(ctx, type);
    inodes.insert(ctx, parentinode, name, inode_no);
    if(type == INODE_DIRECTORY){
        parentinode->entry.num_links++;
        inodes.sync(ctx, parentinode, true);
    }
    inodes.unlock(parentinode);
    inodes.put(ctx, parentinode);

    inode = inodes.get(inode_no);
    inodes.lock(inode);
    inode->entry.major = major;
    inode->entry.minor = minor;
    inode->entry.num_links = 1;
    inodes.sync(ctx, inode, true);
    if(type == INODE_DIRECTORY){
        inodes.insert(ctx, inode, ".", inode_no);
        inodes.insert(ctx, inode, "..", parentinode->inode_no);
    }
    return inode;
}

define_syscall(openat, int dirfd, const char* path, int omode) {
    int fd;
    struct file* f;
    Inode* ip;

    if (!user_strlen(path, 256))
        return -1;

    if (dirfd != AT_FDCWD) {
        printk("sys_openat: dirfd unimplemented\n");
        return -1;
    }

    OpContext ctx;
    bcache.begin_op(&ctx);
    if (omode & O_CREAT) {
        // FIXME: Support acl mode.
        ip = create(path, INODE_REGULAR, 0, 0, &ctx);
        if (ip == 0) {
            bcache.end_op(&ctx);
            return -1;
        }
    } else {
        if ((ip = namei(path, &ctx)) == 0) {
            bcache.end_op(&ctx);
            return -1;
        }
        inodes.lock(ip);
    }

    if ((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0) {
        if (f)
            fileclose(f);
        inodes.unlock(ip);
        inodes.put(&ctx, ip);
        bcache.end_op(&ctx);
        return -1;
    }
    inodes.unlock(ip);
    bcache.end_op(&ctx);

    f->type = FD_INODE;
    f->ip = ip;
    f->off = 0;
    f->readable = !(omode & O_WRONLY);
    f->writable = (omode & O_WRONLY) || (omode & O_RDWR);
    return fd;
}

define_syscall(mkdirat, int dirfd, const char* path, int mode) {
    Inode* ip;
    if (!user_strlen(path, 256))
        return -1;
    if (dirfd != AT_FDCWD) {
        printk("sys_mkdirat: dirfd unimplemented\n");
        return -1;
    }
    if (mode != 0) {
        printk("sys_mkdirat: mode unimplemented\n");
        return -1;
    }
    OpContext ctx;
    bcache.begin_op(&ctx);
    if ((ip = create(path, INODE_DIRECTORY, 0, 0, &ctx)) == 0) {
        bcache.end_op(&ctx);
        return -1;
    }
    inodes.unlock(ip);
    inodes.put(&ctx, ip);
    bcache.end_op(&ctx);
    return 0;
}

define_syscall(mknodat, int dirfd, const char* path, int major, int minor) {
    Inode* ip;
    if (!user_strlen(path, 256))
        return -1;
    if (dirfd != AT_FDCWD) {
        printk("sys_mknodat: dirfd unimplemented\n");
        return -1;
    }
    printk("mknodat: path '%s', major:minor %d:%d\n", path, major, minor);
    OpContext ctx;
    bcache.begin_op(&ctx);
    if ((ip = create(path, INODE_DEVICE, major, minor, &ctx)) == 0) {
        bcache.end_op(&ctx);
        return -1;
    }
    inodes.unlock(ip);
    inodes.put(&ctx, ip);
    bcache.end_op(&ctx);
    return 0;
}

define_syscall(chdir, const char* path) {
    // TODO
    // change the cwd (current working dictionary) of current process to 'path'
    // you may need to do some validations
    OpContext ctx;
    bcache.begin_op(&ctx);
    Inode* inode = namei(path, &ctx);
    if(inode == 0){
        bcache.end_op(&ctx);
        return -1;
    }
    inodes.lock(inode);
    if(inode->entry.type != INODE_DIRECTORY){
        inodes.unlock(inode);
        inodes.put(&ctx, inode);
        bcache.end_op(&ctx);
        return -1;
    }
    inodes.unlock(inode);
    inodes.put(&ctx, thisproc()->cwd);
    bcache.end_op(&ctx);
    thisproc()->cwd = inode;
    return 0;
}

define_syscall(pipe2, int *fd, int flags) {
    // TODO
    File* f0, *f1;
    if(pipeAlloc(&f0, &f1) == -1){
        return -1;
    }
    fd[0] = fdalloc(f0);
    fd[1] = fdalloc(f1);
    flags = flags;
    return 0;
}
