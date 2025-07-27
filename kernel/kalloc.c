// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

struct {
  struct spinlock lock;
  int counts[PHYSTOP / PGSIZE];
} ref;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&ref.lock, "refcount");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE){
    // This is necessary because kfree() now expects to decrement a count.
    // During initialization, this primes the pages for the first free.
    // No lock is needed here as this runs single-threaded at boot.
    ref.counts[(uint64)p / PGSIZE] = 1;
    kfree(p);
  }
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  acquire(&ref.lock);
  if(ref.counts[(uint64)pa / PGSIZE] < 1)
    panic("kfree: ref count is already 0");
  ref.counts[(uint64)pa / PGSIZE]--;
  int count = ref.counts[(uint64)pa / PGSIZE];
  release(&ref.lock);

  if (count == 0) {
    // Fill with junk to catch dangling refs.
    memset(pa, 1, PGSIZE);

    r = (struct run*)pa;

    acquire(&kmem.lock);
    r->next = kmem.freelist;
    kmem.freelist = r;
    release(&kmem.lock);
  }
}

void
add_ref(void *pa)
{
  acquire(&ref.lock);
  if((uint64)pa >= PHYSTOP || (uint64)pa < (uint64)end)
    panic("add_ref: pa out of range");
  if(ref.counts[(uint64)pa / PGSIZE] < 1)
    panic("add_ref: ref count less than 1");

  ref.counts[(uint64)pa / PGSIZE]++;
  release(&ref.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r){
    memset((char*)r, 5, PGSIZE); // fill with junk
    acquire(&ref.lock);
    ref.counts[(uint64)r / PGSIZE] = 1;
    release(&ref.lock);
  }
  return (void*)r;
}