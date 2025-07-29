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
#define HASH(blockno) (blockno % NBUCKETS)

struct {
  struct buf buf[NBUF];
  struct {
    struct spinlock lock;
    struct buf head;
  } buckets[NBUCKETS];
} bcache;

void
binit(void)
{
  struct buf *b;
  char lockname[16];

  for (int i = 0; i < NBUCKETS; i++) {
    snprintf(lockname, sizeof(lockname), "bcache.bucket%d", i);
    initlock(&bcache.buckets[i].lock, lockname);
    bcache.buckets[i].head.prev = &bcache.buckets[i].head;
    bcache.buckets[i].head.next = &bcache.buckets[i].head;
  }

  for(b = bcache.buf; b < bcache.buf + NBUF; b++){
    b->next = bcache.buckets[0].head.next;
    b->prev = &bcache.buckets[0].head;
    initsleeplock(&b->lock, "buffer");
    bcache.buckets[0].head.next->prev = b;
    bcache.buckets[0].head.next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  int target_bucket_idx = HASH(blockno);

  acquire(&bcache.buckets[target_bucket_idx].lock);
  for(b = bcache.buckets[target_bucket_idx].head.next; b != &bcache.buckets[target_bucket_idx].head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.buckets[target_bucket_idx].lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  for(b = bcache.buckets[target_bucket_idx].head.prev; b != &bcache.buckets[target_bucket_idx].head; b = b->prev){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.buckets[target_bucket_idx].lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bcache.buckets[target_bucket_idx].lock);

  for (int i = 0; i < NBUCKETS; i++) {
    if (i == target_bucket_idx) {
      continue;
    }

    acquire(&bcache.buckets[i].lock);
    for (b = bcache.buckets[i].head.next; b != &bcache.buckets[i].head; b = b->next) {
      if (b->refcnt == 0) {
        b->next->prev = b->prev;
        b->prev->next = b->next;
        release(&bcache.buckets[i].lock);

        acquire(&bcache.buckets[target_bucket_idx].lock);

        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        b->refcnt = 1;

        b->next = bcache.buckets[target_bucket_idx].head.next;
        b->prev = &bcache.buckets[target_bucket_idx].head;
        bcache.buckets[target_bucket_idx].head.next->prev = b;
        bcache.buckets[target_bucket_idx].head.next = b;

        release(&bcache.buckets[target_bucket_idx].lock);

        acquiresleep(&b->lock);
        return b;
      }
    }
    release(&bcache.buckets[i].lock);
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

  int bucket_idx = HASH(b->blockno);

  acquire(&bcache.buckets[bucket_idx].lock);
  b->refcnt--;
  release(&bcache.buckets[bucket_idx].lock);
}

void
bpin(struct buf *b) {
  int bucket_idx = HASH(b->blockno);
  acquire(&bcache.buckets[bucket_idx].lock);
  b->refcnt++;
  release(&bcache.buckets[bucket_idx].lock);
}

void
bunpin(struct buf *b) {
  int bucket_idx = HASH(b->blockno);
  acquire(&bcache.buckets[bucket_idx].lock);
  b->refcnt--;
  release(&bcache.buckets[bucket_idx].lock);
}