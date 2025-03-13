// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCKETS 13

struct {
  struct spinlock lock;
  struct buf head;
} bcache_buckets[NBUCKETS];

struct {
  struct spinlock lock;
  struct buf buf[NBUF];

  // // Linked list of all buffers, through prev/next.
  // // Sorted by how recently the buffer was used.
  // // head.next is most recent, head.prev is least.
  // struct buf head;
} bcache;

void
binit(void)
{
  struct buf *b;

  // 初始化 bcache_buckets 的锁，以及链表头
  for(int i = 0; i < NBUCKETS; i++) {
    initlock(&bcache_buckets[i].lock, "bcache");
    bcache_buckets[i].head.prev = &bcache_buckets[i].head;
    bcache_buckets[i].head.next = &bcache_buckets[i].head;
  }

  // 初始化 bcache
  initlock(&bcache.lock, "bcache");
  // // 这是一个环状链表, head.next 最新，head.prev 最老
  // // Create linked list of buffers
  // bcache.head.prev = &bcache.head;
  // bcache.head.next = &bcache.head;
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    // 初始化时，每一个 buffer 的 next 和 prev 都是自己
    b->next = b;
    b->prev = b;
    // b->next = bcache.head.next;
    // b->prev = &bcache.head;
    initsleeplock(&b->lock, "buffer");
    // bcache.head.next->prev = b;
    // bcache.head.next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  acquire(&bcache.lock);

  // 获取对应哈希桶的锁
  uint hash_idx = blockno % NBUCKETS;
  acquire(&bcache_buckets[hash_idx].lock);
  for(b = bcache_buckets[hash_idx].head.next; b != &bcache_buckets[hash_idx].head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache_buckets[hash_idx].lock);
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bcache_buckets[hash_idx].lock);

  // 为了避免和上面的代码产生 refcnt 冲突，每个 buffer 在判断 b->refcnt 之前要获取这个 b 对应的桶的锁
  uint original_hash_idx;
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    if(b->refcnt == 0) {
      b->dev = dev;
      original_hash_idx = b->blockno % NBUCKETS;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      break;
    }
  }

  // 执行到这里，若 b < bcache.buf+NBUF，说明没找到所需的 buffer，否则说明没找到
  if (b < bcache.buf+NBUF) {
    // 首先要把该 buffer 从原来的链表上脱离
    if(original_hash_idx != hash_idx) {
      acquire(&bcache_buckets[original_hash_idx].lock);
    }
    acquire(&bcache_buckets[hash_idx].lock);
    b->next->prev = b->prev;
    b->prev->next = b->next;
    // 随后把该 buffer 放进 hash_idx 的桶里，方便被访问相同块的进程使用缓存
    b->next = bcache_buckets[hash_idx].head.next;
    b->prev = &bcache_buckets[hash_idx].head;
    bcache_buckets[hash_idx].head.next->prev = b;
    bcache_buckets[hash_idx].head.next = b;
    // 释放 桶 的锁，返回这个 buffer
    if(original_hash_idx != hash_idx)
      release(&bcache_buckets[original_hash_idx].lock);
    release(&bcache_buckets[hash_idx].lock);
    acquiresleep(&b->lock);
    release(&bcache.lock);
    return b;
  }
  
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  // 只加对应哈希桶的锁
  uint hash_idx = b->blockno % NBUCKETS;
  acquire(&bcache_buckets[hash_idx].lock);
  b->refcnt--;
  // 这里不需要做链表操作，把该 buffer 留在它所在的桶里就行
  release(&bcache_buckets[hash_idx].lock);
}

void
bpin(struct buf *b) {
  // 只加对应哈希桶的锁
  uint hash_idx = b->blockno % NBUCKETS;
  acquire(&bcache_buckets[hash_idx].lock);
  b->refcnt++;
  release(&bcache_buckets[hash_idx].lock);
}

void
bunpin(struct buf *b) {
  // 只加对应哈希桶的锁
  uint hash_idx = b->blockno % NBUCKETS;
  acquire(&bcache_buckets[hash_idx].lock);
  b->refcnt--;
  release(&bcache_buckets[hash_idx].lock);
}


