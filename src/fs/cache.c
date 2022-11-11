#include <common/bitmap.h>
#include <common/string.h>
#include <fs/cache.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <kernel/proc.h>

static const SuperBlock* sblock;
static const BlockDevice* device;

static SpinLock lock;     // protects block cache.
static ListNode head;     // the list of all allocated in-memory block.
static LogHeader header;  // in-memory copy of log header block.

// hint: you may need some other variables. Just add them here.
struct LOG {
    /* data */
    SpinLock lock;
    Semaphore begin;
    Semaphore end;
    u32 log_used;
    u32 log_size;
    u32 outstanding;
} log;

// read the content from disk.
static INLINE void device_read(Block* block) {
    device->read(block->block_no, block->data);
}

// write the content back to disk.
static INLINE void device_write(Block* block) {
    device->write(block->block_no, block->data);
}

// read log header from disk.
static INLINE void read_header() {
    device->read(sblock->log_start, (u8*)&header);
}

// write log header back to disk.
static INLINE void write_header() {
    device->write(sblock->log_start, (u8*)&header);
}

// initialize a block struct.
static void init_block(Block* block) {
    block->block_no = 0;
    init_list_node(&block->node);
    block->acquired = false;
    block->pinned = false;

    init_sleeplock(&block->lock);
    block->valid = false;
    memset(block->data, 0, sizeof(block->data));
}

// see `cache.h`.
static usize get_num_cached_blocks() {
    // TODO
    int num = 0;
    _for_in_list(node, &head){
        if(node == &head) continue;
        num ++;
    }
    return num;
}

// see `cache.h`.
static Block* cache_acquire(usize block_no) {
    // TODO
start:
    _acquire_spinlock(&lock);
    _for_in_list(p, &head){
        if(p == &head) continue;
        Block* b = container_of(p, Block, node);
        if(b->block_no == block_no){
            if(!b->acquired){
                get_sem(&b->lock);
                _detach_from_list(p);
                _insert_into_list(&head, p);
                b->acquired = true;
                _release_spinlock(&lock);
                return b;
            }else{
                _release_spinlock(&lock);
                unalertable_wait_sem(&b->lock);
                goto start;
            }
        }
    }
    usize cnum = get_num_cached_blocks();
    if(cnum >= EVICTION_THRESHOLD){
        ListNode* p = head.prev;
        while(p != &head && cnum >= EVICTION_THRESHOLD){
            Block* b = container_of(p, Block, node);
            if(!b->pinned && !b->acquired){
                p = _detach_from_list(p);
                kfree(b);
                cnum--;
            }else{
                p = p->prev;
            }
        }
    }
    Block* block = kalloc(sizeof(Block));
    init_block(block);
    block->block_no = block_no;
    block->valid = true;
    block->acquired = true;
    _insert_into_list(&head, &block->node);
    unalertable_wait_sem(&block->lock);
    _release_spinlock(&lock);
    device_read(block);
    return block;
}

// see `cache.h`.
static void cache_release(Block* block) {
    // TODO
    _acquire_spinlock(&lock);
    block->acquired = false;
    post_sem(&block->lock);
    _release_spinlock(&lock);
}

static void log_wb(){
    for(usize i = 0; i < header.num_blocks; i++){
        Block* logb = cache_acquire(sblock->log_start + i + 1);
        Block* sdb = cache_acquire(header.block_no[i]);
        memmove(sdb->data, logb->data, BLOCK_SIZE);
        device_write(sdb);
        sdb->pinned = false;
        cache_release(logb);
        cache_release(sdb);
    }
    header.num_blocks = 0;
    write_header();
}

// initialize block cache.
void init_bcache(const SuperBlock* _sblock, const BlockDevice* _device) {
    sblock = _sblock;
    device = _device;

    // TODO
    init_spinlock(&lock);
    init_list_node(&head);
    
    init_spinlock(&log.lock);
    init_sem(&log.begin, 0);
    init_sem(&log.end, 0);
    log.log_used = 0;
    log.log_size = MIN(LOG_MAX_SIZE, sblock->num_log_blocks - 1);
    log.outstanding = 0;

    read_header();
    log_wb();
}

// see `cache.h`.
static void cache_begin_op(OpContext* ctx) {
    // TODO
    _acquire_spinlock(&log.lock);
    while(log.log_used + OP_MAX_NUM_BLOCKS > log.log_size){
        _lock_sem(&log.begin);
        _release_spinlock(&log.lock);
        _wait_sem(&log.begin, false);
        _acquire_spinlock(&log.lock);
    }
    ctx->rm = OP_MAX_NUM_BLOCKS;
    log.log_used += OP_MAX_NUM_BLOCKS;
    log.outstanding++;
    _release_spinlock(&log.lock);
}

// see `cache.h`.
static void cache_sync(OpContext* ctx, Block* block) {
    // TODO
    if(ctx){
        usize i;
        _acquire_spinlock(&log.lock);
        for(i = 0; i < header.num_blocks; i++){
            if(header.block_no[i] == block->block_no)
                break;
        }
        header.block_no[i] = block->block_no;
        if(i == header.num_blocks){
            if(ctx->rm == 0)
                PANIC();
            ctx->rm--;
            header.num_blocks++;
            block->pinned = true;
        }
        _release_spinlock(&log.lock);
    }else{
        device_write(block);
    }
}

// see `cache.h`.
static void cache_end_op(OpContext* ctx) {
    // TODO
    _acquire_spinlock(&log.lock);
    log.log_used -= ctx->rm;
    ctx->rm = 0;
    log.outstanding--;
    if(log.outstanding == 0){
        for(usize i = 0; i < header.num_blocks; i++){
            Block* logb = cache_acquire(sblock->log_start + i + 1);
            Block* sdb = cache_acquire(header.block_no[i]);
            memmove(logb->data, sdb->data, BLOCK_SIZE);
            device_write(logb);
            cache_release(logb);
            cache_release(sdb);
        }
        write_header();
        log.log_used -= header.num_blocks;
        log_wb();
        post_all_sem(&log.end);
        post_all_sem(&log.begin);
        _release_spinlock(&log.lock);
    }else{
        post_all_sem(&log.begin);
        _lock_sem(&log.end);
        _release_spinlock(&log.lock);
        _wait_sem(&log.end, false);
    }
}

// see `cache.h`.
// hint: you can use `cache_acquire`/`cache_sync` to read/write blocks.
static usize cache_alloc(OpContext* ctx) {
    // TODO
    for(u32 i = 0; i < sblock->num_blocks; i += BIT_PER_BLOCK){
        Block* b = cache_acquire(sblock->bitmap_start + i / BIT_PER_BLOCK);
        BitmapCell* bm = b->data;
        for(u32 j = 0; j < BIT_PER_BLOCK && i + j < sblock->num_blocks; j++){
            if(!bitmap_get(bm, j)){
                bitmap_set(bm, j);
                cache_sync(ctx, b);
                cache_release(b);
                Block* new = cache_acquire(i + j);
                memset(new->data, 0, BLOCK_SIZE);
                cache_sync(ctx, new);
                cache_release(new);
                return i + j;
            }
        }
        cache_release(b);
    }
    PANIC();
}

// see `cache.h`.
// hint: you can use `cache_acquire`/`cache_sync` to read/write blocks.
static void cache_free(OpContext* ctx, usize block_no) {
    // TODO
    Block* b = cache_acquire(sblock->bitmap_start + block_no / BIT_PER_BLOCK);
    BitmapCell* bm = b->data;
    bitmap_clear(bm, block_no % BIT_PER_BLOCK);
    cache_sync(ctx, b);
    cache_release(b);
}

BlockCache bcache = {
    .get_num_cached_blocks = get_num_cached_blocks,
    .acquire = cache_acquire,
    .release = cache_release,
    .begin_op = cache_begin_op,
    .sync = cache_sync,
    .end_op = cache_end_op,
    .alloc = cache_alloc,
    .free = cache_free,
};
