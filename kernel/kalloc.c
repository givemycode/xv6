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

struct pagerefcnt {
  struct spinlock lock;
  uint8 refcount[PHYSTOP / PGSIZE];
} ref;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&ref.lock, "ref"); //初始化自旋锁
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE){
    // 这里设置为1再kfree就变成0了
    ref.refcount[(uint64)p / PGSIZE] = 1; 
    kfree(p);
  }
}


// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");
  
  // 获取自旋锁以确保对ref.refcount数组的访问是线程安全的
  acquire(&ref.lock);
  // 减少物理页面的引用计数。
  // pa 是物理地址，通过 (uint64)pa / PGSIZE 计算出对应页面在 ref.refcount 数组中的索引
  if(--ref.refcount[(uint64)pa / PGSIZE] > 0) {
    // 释放自旋锁，允许其他线程访问 ref.refcount 数组
    release(&ref.lock);
    return;
  }
  release(&ref.lock);

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
  if(r){
    kmem.freelist = r->next;
    acquire(&ref.lock);
    ref.refcount[(uint64)r / PGSIZE] = 1; 
    release(&ref.lock);
  }
    
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}



void incref(uint64 va) {
  acquire(&ref.lock);
  if(va < 0 || va > PHYSTOP){
      panic("wrong virtual address");
  }
  ref.refcount[va / PGSIZE]++;
  release(&ref.lock);
}
