
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
  struct run *freelist;
} kmem_super;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&kmem_super.lock, "kmem_super");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)(pa_end - SUPERPAGE_MEM); p += PGSIZE)
    kfree(p);

  p = (char *)SUPERPGROUNDUP((uint64)p);
  for (; p + SUPERPGSIZE <= (char *)pa_end; p += SUPERPGSIZE)
    superkfree(p);
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

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
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

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

// Free a super page
void
superkfree(void *pa)
{
  struct run *r;

  if (((uint64)pa % SUPERPGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
    panic("superkfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run *)pa;

  acquire(&kmem_super.lock);
  r->next = kmem_super.freelist;
  kmem_super.freelist = r;
  release(&kmem_super.lock);
}

// Allocate a super page
void *
superkalloc(void)
{
  struct run *r;
  acquire(&kmem_super.lock);
  r = kmem_super.freelist;
  if (r)
    kmem_super.freelist = r->next;
  release(&kmem_super.lock);

  if (r)
    memset((char *)r, 5, SUPERPGSIZE); // fill with junk
  return (void *)r;
}
