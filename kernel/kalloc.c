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

// 匿名结构体，直接用来声明了一个名为kmems的数组
struct {
  struct spinlock lock;
  // freelist 通常指向空闲内存块的链表头
  struct run *freelist; 
} kmems[NCPU];// NCPU 是一个宏或常量，定义了系统中CPU的数量

void
kinit()
{
  for(int i=0; i<NCPU; i++){
    // 初始化kmems[i].lock这个spinlock（自旋锁）
    initlock(&kmems[i].lock, "kmem");
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

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
// 有进程来归还 page ，就将 page 挂载到相应的 CPU 的 freelist 上
void
kfree(void *pa)
{
  struct run *r;
  
  // 地址是否页面对齐 || 是否低于内核结束地址 || 是否超出物理内存范围
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  // 初始化内存，将其设置为某个特定的值
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;
  
  // 获取 cpuid 时要关中断，完事之后再把中断开下来
  push_off();
  int id = cpuid();
  pop_off();
  
  // 将空闲的 page 归还给第 id 个 CPU
  acquire(&kmems[id].lock);
  r->next = kmems[id].freelist;
  kmems[id].freelist = r;
  release(&kmems[id].lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
// 主要流程，就是去第 id 号 CPU 中拿取空闲 page 。
// 如果该 CPU 有空闲 page ，则直接分配
// 反之，就去偷别的 CPU 的 ，去看看别的 CPU 是否有空闲 page 
// 如果偷也没偷到，那么就向上层返回空指针
void *
kalloc(void)
{
  struct run *r;
  // 获取 cpuid 时要关中断，完事之后再把中断开下来
  
  // 禁用当前 CPU 的中断
  push_off();
  // 返回当前运行代码的 CPU 的标识符
  int id = cpuid();
  // 内核会递减禁用中断的嵌套计数
  // 如果这是最后一次调用 pop_off()，那么中断将被重新启用
  pop_off();
  // 尝试获取剩余的空闲page

  // 获取一个自旋锁
  acquire(&kmems[id].lock);
  // r现在指向了当前CPU上的第一个空闲内存块
  r = kmems[id].freelist;

  if(r){
    // 链表头更新为 r->next，系统从链表中“取走”了一个内存块
    kmems[id].freelist = r->next;
  }else{
    // 当前 CPU 上没有空闲内存块可用

    // 在其他 CPU 上查找是否有空闲的内存块
    for(int i=0; i<NCPU; i++){
      // 避免在当前 CPU 上重复执行已经处理过的逻辑
      if(i == id){
        continue;
      }
      // 尝试偷一个其他 CPU 的空闲 page
      // 确保了在访问或修改 kmems[i]中的 freelist 时，当前 CPU 不会被其他线程或 CPU 中断
      acquire(&kmems[i].lock);
      if(!kmems[i].freelist) {
        release(&kmems[i].lock);
        continue;
      }  
      r = kmems[i].freelist;
      kmems[i].freelist = r->next;
      release(&kmems[i].lock);
      break;
    }
  }
  release(&kmems[id].lock);
  // 有一种可能：第id个CPU没有空闲page，也没偷到
  if(r){
    memset((char*)r, 5, PGSIZE); // fill with junk
  }    
  return (void*)r;
}
