#include <common/string.h>
#include <fs/inode.h>
#include <kernel/mem.h>
#include <kernel/printk.h>

// this lock mainly prevents concurrent access to inode list `head`, reference
// count increment and decrement.
static SpinLock lock;
static ListNode head;

static const SuperBlock* sblock;
static const BlockCache* cache;

// return which block `inode_no` lives on.
static INLINE usize to_block_no(usize inode_no) {
    return sblock->inode_start + (inode_no / (INODE_PER_BLOCK));
}

// return the pointer to on-disk inode.
static INLINE InodeEntry* get_entry(Block* block, usize inode_no) {
    return ((InodeEntry*)block->data) + (inode_no % INODE_PER_BLOCK);
}

// return address array in indirect block.
static INLINE u32* get_addrs(Block* block) {
    return ((IndirectBlock*)block->data)->addrs;
}

// initialize inode tree.
void init_inodes(const SuperBlock* _sblock, const BlockCache* _cache) {
    init_spinlock(&lock);
    init_list_node(&head);
    sblock = _sblock;
    cache = _cache;

    if (ROOT_INODE_NO < sblock->num_inodes)
        inodes.root = inodes.get(ROOT_INODE_NO);
    else
        printk("(warn) init_inodes: no root inode.\n");
}

// initialize in-memory inode.
static void init_inode(Inode* inode) {
    init_sleeplock(&inode->lock);
    init_rc(&inode->rc);
    init_list_node(&inode->node);
    inode->inode_no = 0;
    inode->valid = false;
}

// see `inode.h`.
static usize inode_alloc(OpContext* ctx, InodeType type) {
    ASSERT(type != INODE_INVALID);

    // TODO
    for(u32 i = 1; i < sblock->num_inodes; i++){
        Block* b = cache->acquire(to_block_no(i));
        InodeEntry* entry = get_entry(b, i);
        if(entry->type == INODE_INVALID){
            memset(entry, 0, sizeof(InodeEntry));
            entry->type = type;
            cache->sync(ctx, b);
            cache->release(b);
            return i;
        }
        cache->release(b);
    }
    PANIC();
    return 0;
}

// see `inode.h`.
static void inode_lock(Inode* inode) {
    ASSERT(inode->rc.count > 0);
    // TODO
    ASSERT(wait_sem(&inode->lock));
}

// see `inode.h`.
static void inode_unlock(Inode* inode) {
    ASSERT(inode->rc.count > 0);
    // TODO
    post_sem(&inode->lock);
}

// see `inode.h`.
static void inode_sync(OpContext* ctx, Inode* inode, bool do_write) {
    // TODO
    Block* b = cache->acquire(to_block_no(inode->inode_no));
    InodeEntry* entry = get_entry(b, inode->inode_no);
    if(inode->valid && do_write){
        memmove(entry, &inode->entry, sizeof(InodeEntry));
        cache->sync(ctx, b);
    }else if(!inode->valid){
        memmove(&inode->entry, entry, sizeof(InodeEntry));
        inode->valid = true;
    }
    cache->release(b);
}

// see `inode.h`.
static Inode* inode_get(usize inode_no) {
    ASSERT(inode_no > 0);
    ASSERT(inode_no < sblock->num_inodes);
    _acquire_spinlock(&lock);
    // TODO
    Inode* inode;

    _for_in_list(p, &head){
        if(p == &head) continue;
        inode = container_of(p, Inode, node);
        if(inode->inode_no == inode_no){
            _increment_rc(&inode->rc);
            _release_spinlock(&lock);
            inode_lock(inode);
            inode_unlock(inode);
            return inode;
        }
    }

    inode = kalloc(sizeof(Inode));
    init_inode(inode);
    inode->inode_no = inode_no;
    _increment_rc(&inode->rc);
    _insert_into_list(&head, &inode->node);

    inode_lock(inode);
    _release_spinlock(&lock);
    inode_sync(NULL, inode, false);
    inode_unlock(inode);

    ASSERT(inode->entry.type != INODE_INVALID);
    return inode;
}
// see `inode.h`.
static void inode_clear(OpContext* ctx, Inode* inode) {
    // TODO
    auto entry = &inode->entry;
    for(u32 i = 0; i < INODE_NUM_DIRECT; i++){
        if(entry->addrs[i] != NULL){
            cache->free(ctx, entry->addrs[i]);
            entry->addrs[i] = NULL;
        }
    }
    if(entry->indirect != NULL){
        auto b = cache->acquire(entry->indirect);
        auto addrs = get_addrs(b);
        for(usize i = 0; i < INODE_NUM_INDIRECT; i++){
            if(addrs[i] != NULL){
                cache->free(ctx, addrs[i]);
            }
        }
        cache->release(b);
        cache->free(ctx, entry->indirect);
        entry->indirect = NULL;
    }
    entry->num_bytes = 0;
    inode_sync(ctx, inode, true);
}

// see `inode.h`.
static Inode* inode_share(Inode* inode) {
    // TODO
    _acquire_spinlock(&lock);
    _increment_rc(&inode->rc);
    _release_spinlock(&lock);
    return inode;
}

// see `inode.h`.
static void inode_put(OpContext* ctx, Inode* inode) {
    // TODO
    _acquire_spinlock(&lock);
    if(inode->rc.count == 1 && inode->entry.num_links == 0 && inode->valid){
        _detach_from_list(&inode->node);
        inode_lock(inode);
        _release_spinlock(&lock);
        inode_clear(ctx, inode);
        inode->entry.type = INODE_INVALID;
        inode_sync(ctx, inode, true);
        inode->valid = false;
        inode_unlock(inode);
        kfree(inode);
        return;
    }
    _decrement_rc(&inode->rc);
    _release_spinlock(&lock);
}

// this function is private to inode layer, because it can allocate block
// at arbitrary offset, which breaks the usual file abstraction.
//
// retrieve the block in `inode` where offset lives. If the block is not
// allocated, `inode_map` will allocate a new block and update `inode`, at
// which time, `*modified` will be set to true.
// the block number is returned.
//
// NOTE: caller must hold the lock of `inode`.
static usize inode_map(OpContext* ctx,
                       Inode* inode,
                       usize offset,
                       bool* modified) {
    // TODO
    u32 block_no;
    auto entry = &inode->entry;
    *modified = false;
    if(offset < INODE_NUM_DIRECT){
        if(entry->addrs[offset] == NULL){
            entry->addrs[offset] = cache->alloc(ctx);
            *modified = true;
        }
        block_no = entry->addrs[offset];
    }else if(offset < INODE_NUM_DIRECT + INODE_NUM_INDIRECT){
        offset -= INODE_NUM_DIRECT;
        if(entry->indirect == NULL){
            entry->indirect = cache->alloc(ctx);
        }
        auto b = cache->acquire(entry->indirect);
        auto addrs = get_addrs(b);
        if(addrs[offset] == NULL){
            addrs[offset] = cache->alloc(ctx);
            cache->sync(ctx, b);
            *modified = true;
        }
        block_no = addrs[offset];
        cache->release(b);
    }else{
        PANIC();
    }
    return block_no;
}

// see `inode.h`.
static usize inode_read(Inode* inode, u8* dest, usize offset, usize count) {
    InodeEntry* entry = &inode->entry;
    if (count + offset > entry->num_bytes)
        count = entry->num_bytes - offset;
    usize end = offset + count;
    ASSERT(offset <= entry->num_bytes);
    ASSERT(end <= entry->num_bytes);
    ASSERT(offset <= end);

    // TODO
    if(count == 0) return count;
    count = 0;
    for(usize i = offset/BLOCK_SIZE; i <= (end-1)/BLOCK_SIZE; i++){
        usize n = MIN(end - offset, (i + 1) * BLOCK_SIZE - offset);
        bool modified;
        auto block_no = inode_map(NULL, inode, i, &modified);
        auto b = cache->acquire(block_no);
        memmove(dest + count, b->data + offset % BLOCK_SIZE, n);
        cache->release(b);
        offset += n;
        count += n;
    }
    return count;
}

// see `inode.h`.
static usize inode_write(OpContext* ctx,
                         Inode* inode,
                         u8* src,
                         usize offset,
                         usize count) {
    InodeEntry* entry = &inode->entry;
    usize end = offset + count;
    ASSERT(offset <= entry->num_bytes);
    ASSERT(end <= INODE_MAX_BYTES);
    ASSERT(offset <= end);

    // TODO
    count = 0;
    for(usize i = offset/BLOCK_SIZE; i <= (end-1)/BLOCK_SIZE; i++){
        usize n = MIN(end - offset, (i + 1) * BLOCK_SIZE - offset);
        bool modified;
        auto block_no = inode_map(ctx, inode, i, &modified);
        auto b = cache->acquire(block_no);
        memmove(b->data + offset % BLOCK_SIZE, src + count, n);
        cache->sync(ctx, b);
        cache->release(b);
        offset += n;
        count += n;
    }
    if(end > entry->num_bytes){
        entry->num_bytes = end;
        inode_sync(ctx, inode, true);
    }
    return count;
}

// see `inode.h`.
static usize inode_lookup(Inode* inode, const char* name, usize* index) {
    InodeEntry* entry = &inode->entry;
    ASSERT(entry->type == INODE_DIRECTORY);

    // TODO
    for(usize offset = 0; offset < entry->num_bytes; offset += sizeof(DirEntry)){
        DirEntry de;
        inode_read(inode, (u8*)&de, offset, sizeof(DirEntry));
        if(de.inode_no != 0 && strncmp(name, de.name, FILE_NAME_MAX_LENGTH) == 0){
            if(index != NULL) *index = offset;
            return de.inode_no;
        }
    }

    return 0;
}

// see `inode.h`.
static usize inode_insert(OpContext* ctx,
                          Inode* inode,
                          const char* name,
                          usize inode_no) {
    InodeEntry* entry = &inode->entry;
    ASSERT(entry->type == INODE_DIRECTORY);

    // TODO
    usize index;
    if(inode_lookup(inode, name, &index) != 0){
        return -1;
    }

    DirEntry de;
    u32 offset = 0;
    for(offset = 0; offset < entry->num_bytes; offset += sizeof(DirEntry)){
        inode_read(inode, (u8*)&de, offset, sizeof(DirEntry));
        if(de.inode_no == 0){
            break;
        }
    }

    de.inode_no = inode_no;
    memmove(de.name, name, FILE_NAME_MAX_LENGTH);
    inode_write(ctx, inode, (u8*)&de, offset, sizeof(DirEntry));
    return offset;
}

// see `inode.h`.
static void inode_remove(OpContext* ctx, Inode* inode, usize index) {
    // TODO
    ASSERT(index%sizeof(DirEntry) == 0);
    if(index < inode->entry.num_bytes){
        DirEntry de = {0};
        inode_write(ctx, inode, (u8*)&de, index, sizeof(DirEntry));
    }
}

InodeTree inodes = {
    .alloc = inode_alloc,
    .lock = inode_lock,
    .unlock = inode_unlock,
    .sync = inode_sync,
    .get = inode_get,
    .clear = inode_clear,
    .share = inode_share,
    .put = inode_put,
    .read = inode_read,
    .write = inode_write,
    .lookup = inode_lookup,
    .insert = inode_insert,
    .remove = inode_remove,
};
