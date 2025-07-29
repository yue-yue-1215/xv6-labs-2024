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
  char name[16]; 
} kmem[NCPU];

void
kinit()
{
  for(int i = 0; i < NCPU; i++){
    snprintf(kmem[i].name, 16, "kmem_%d", i);
    initlock(&kmem[i].lock, kmem[i].name);
  }
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
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

  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  push_off();
  int cid = cpuid();
  pop_off();

  acquire(&kmem[cid].lock);
  r->next = kmem[cid].freelist;
  kmem[cid].freelist = r;
  release(&kmem[cid].lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  push_off();
  int my_cpu = cpuid();
  pop_off();

  acquire(&kmem[my_cpu].lock);
  r = kmem[my_cpu].freelist;
  if(r) {
    kmem[my_cpu].freelist = r->next;
  }
  release(&kmem[my_cpu].lock);

  if(r){
    memset((char*)r, 5, PGSIZE);
    return (void*)r;
  }

  for(int i = 0; i < NCPU; i++){
    if(i == my_cpu) {
      continue;
    }

    struct run *stolen_list = 0;

    acquire(&kmem[i].lock);
    stolen_list = kmem[i].freelist;
    if(stolen_list){
      kmem[i].freelist = 0;
    }
    release(&kmem[i].lock);

    if(stolen_list){
      acquire(&kmem[my_cpu].lock);
      kmem[my_cpu].freelist = stolen_list;

      r = kmem[my_cpu].freelist;
      kmem[my_cpu].freelist = r->next;
      release(&kmem[my_cpu].lock);

      memset((char*)r, 5, PGSIZE);
      return (void*)r;
    }
  }

  return 0;
}