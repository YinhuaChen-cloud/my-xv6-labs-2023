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

  // 打印每个桶的 buffer 数量，以及数组中空闲的 buffer 数量
  int free_count = 0;
  for(int i = 0; i < NBUF; i++) {
    if(bcache.buf[i].refcnt == 0) {
      free_count++;
    }
  }

  // 先在哈希表对应的桶里看能否找到所需的 buffer
  uint hash_idx = blockno % NBUCKETS;

  printf("in bget, blockno = %d, hash_idx = %d, free_count = %d\n", blockno, hash_idx, free_count);

  acquire(&bcache_buckets[hash_idx].lock);
  // 如果在桶里能找到对应的 buffer，则在得到这个 buffer 的锁后返回这个 buffer
  for(b = bcache_buckets[hash_idx].head.next; b != &bcache_buckets[hash_idx].head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache_buckets[hash_idx].lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  printf("in bget, blockno = %d, hash_idx = %d, is not in bucket\n", blockno, hash_idx);

  // 这里不能释放锁，为了对应 
  // "3. Searching in the hash table for a buffer and allocating an entry for that buffer when the 
  // buffer is not found must be atomic. "
  // 实际上是为了避免在 brelse 执行过程中，refcnt 被修改

  // 若在哈希桶中找不到 buffer，则在 buffer 数组中寻找一个 refcnt == 0 的 buffer，获取它的锁，并返回这个 buffer 
  // (brelse 再把 buffer 给对应的桶)
  // 直接在全局数组里找 buffer，有可能会把其它 bucket 里的空闲 buffer 在 brelse 阶段放到我们的 bucket 里
  // 先获取 bcache 的锁，避免 refcnt 读写竞争冲突
  acquire(&bcache.lock);
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      printf("in bget, blockno = %d, hash_idx = %d, b->refcnt = %d\n", blockno, hash_idx, b->refcnt);
      release(&bcache.lock);
      release(&bcache_buckets[hash_idx].lock);
      acquiresleep(&b->lock);
      return b;
    }
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

  // 计算哈希索引
  uint hash_idx = b->blockno % NBUCKETS;

  printf("in brelse, b->blockno = %d, hash_idx = %d\n", b->blockno, hash_idx);

  // 获取对应桶的锁，随后把 buffer 放入对应桶中
  acquire(&bcache_buckets[hash_idx].lock);
  b->refcnt--;
  printf("after b->refcnt--, b->refcnt = %d\n", b->refcnt);
  // 若索引归零，放进对应桶的链表里
  if (b->refcnt == 0) {
    // 先把 buffer 从原来的链表中取出
    b->next->prev = b->prev;
    b->prev->next = b->next;
    // 随后把 buffer 放到对应桶的链表头
    b->next = bcache_buckets[hash_idx].head.next;
    b->prev = &bcache_buckets[hash_idx].head;
    bcache_buckets[hash_idx].head.next->prev = b;
    bcache_buckets[hash_idx].head.next = b;
  }
  
  release(&bcache_buckets[hash_idx].lock);
}

void
bpin(struct buf *b) {
  printf("in bpin, b->blockno = %d\n", b->blockno);
  // 计算哈希索引
  uint hash_idx = b->blockno % NBUCKETS;
  acquire(&bcache_buckets[hash_idx].lock);
  b->refcnt++;
  release(&bcache_buckets[hash_idx].lock);
}

void
bunpin(struct buf *b) {
  printf("in bunpin, b->blockno = %d\n", b->blockno);
  // 计算哈希索引
  uint hash_idx = b->blockno % NBUCKETS;
  acquire(&bcache_buckets[hash_idx].lock);
  b->refcnt--;
  release(&bcache_buckets[hash_idx].lock);
}


