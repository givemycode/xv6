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

struct {
    struct buf buf[NBUF];
    // NBUCKET定义了哈希表的桶数
    // 每个桶通常存储同一哈希值的缓冲区链表，用来加速缓冲区查找
    struct buf buckets[NBUCKET];
    // 每个桶对应一个自旋锁
    struct spinlock lks[NBUCKET];
} bcache;

static void bufinit(struct buf* b, uint dev, uint blockno)
{
  b->dev = dev;
  b->blockno = blockno;
  b->valid = 0;
  b->refcnt = 1;
}
static int
myhash(int x)
{
  return x%NBUCKET;
}



void binit(void)
{
  struct buf *b;

  for(int i=0; i<NBUCKET; i++){
    initlock(&bcache.lks[i], "bcache");
  } 
  
   // Create linked list of buffers
  for(int i=0; i<NBUCKET; i++) {
    bcache.buckets[i].prev = &bcache.buckets[i];
    bcache.buckets[i].next = &bcache.buckets[i];
  }

  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->next = bcache.buckets[0].next;
    b->prev = &bcache.buckets[0];
    initsleeplock(&b->lock, "buffer");
    bcache.buckets[0].next->prev = b;
    bcache.buckets[0].next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf* bget(uint dev, uint blockno)
{
  struct buf *b;
  int id = myhash(blockno);
  acquire(&bcache.lks[id]);

  // 用来在缓冲区缓存的哈希桶中查找特定的缓冲区块

  // 循环双向链表，遍历链表的结束条件是 b 再次等于头节点地址
  for(b = bcache.buckets[id].next; b != &bcache.buckets[id]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      // 表示当前线程正在使用这个缓冲区块，防止它被其他线程释放
      b->refcnt++;
      // 允许其他线程访问该哈希桶
      release(&bcache.lks[id]);
      // 获取找到的缓冲区块 b 的睡眠锁，确保当前线程拥有对缓冲区块 b 的独占访问权
      acquiresleep(&b->lock);
      // 找到匹配的缓冲区块后，函数返回指向该缓冲区块的指针 b
      return b;
    }
  }
  
  // 初始化一个指针 victm，用于指向将要被淘汰的缓冲区块
  // 初始值为 0，表示尚未找到需要淘汰的块
  struct buf *victm = 0;
  // ticks 是系统的时间戳，用于记录系统的当前时间
  uint minticks = ticks;
  // 遍历链表中的每个缓冲区块，寻找未被使用且是最早使用过的块
  for(b = bcache.buckets[id].next; b != &bcache.buckets[id]; b = b->next){
    // 检查当前缓冲区块 b 是否未被使用（即 refcnt == 0），并且其 lastuse 时间戳小于等于当前最小时间戳 minticks
    if(b->refcnt==0 && b->lastuse<=minticks) {
      minticks = b->lastuse;
      // victm 指向当前缓冲区块 b
      victm = b;
    }
  }
  if(!victm) 
    goto steal;

  // 将找到的或回收的缓冲区块 victm 重新初始化为特定的设备号和块号，使其可以用于新的磁盘操作
  bufinit(victm, dev, blockno);
  // 在完成对哈希桶的操作后，释放锁以允许其他线程访问该哈希桶
  release(&bcache.lks[id]);
  // 在返回之前，获取该缓冲区块的睡眠锁，以确保接下来的操作能够安全进行。
  acquiresleep(&victm->lock);
  // 返回已初始化并锁定的缓冲区块指针，供调用者使用
  return victm;
  
steal:
  // 到别的哈希桶挖buf
  for(int i=0; i<NBUCKET; i++){
    if(i == id){
      continue;
    }
    acquire(&bcache.lks[i]); 
    // 重置 minticks 为当前的时间戳 ticks，用于在新的哈希桶中查找最久未使用的块
    minticks = ticks;
    for(b = bcache.buckets[i].next; b != &bcache.buckets[i]; b = b->next){
      if(b->refcnt == 0 && b->lastuse <= minticks) {
        minticks = b->lastuse;
        victm = b;
      }
    }
    // 如果在当前哈希桶中没有找到合适的缓冲区块，释放锁并继续检查下一个哈希桶
    if(!victm) {
      release(&bcache.lks[i]);
      continue;
    }
    bufinit(victm, dev, blockno);
    // 将 victm 从第 i 号哈希桶中取出来 
    victm->next->prev = victm->prev;
    victm->prev->next = victm->next;
    release(&bcache.lks[i]);

    // 主要目的是将缓冲区块 victm 插入到目标哈希桶 id 的链表头部
    // ‘victm’指向链表的第一个节点前的节点
    victm->next = bcache.buckets[id].next;
    // 让链表中的原第一个节点的 prev 指针指向 ‘victm’
    bcache.buckets[id].next->prev = victm;
    // 将目标哈希桶的链表头指针指向 ‘victm’
    bcache.buckets[id].next = victm;
    victm->prev = &bcache.buckets[id];
    release(&bcache.lks[id]);
    acquiresleep(&victm->lock);
    return victm;
  }
  release(&bcache.lks[id]);
  panic("bget: no buf");
}

// Return a locked buf with the contents of the indicated block.
struct buf* bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  // 如果buf中的数据过时了，那么需要重新读取
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
// 将 Buffer cache 中的数据写回至 disk 中
void bwrite(struct buf *b)
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
  // 为了确保在释放缓冲区块之前，当前线程确实持有该锁
  if(!holdingsleep(&b->lock))
    panic("brelse");

  // 释放缓冲区块 b 的睡眠锁，释放锁后，其他线程可以访问这个缓冲区块
  releasesleep(&b->lock);
  
  // 计算缓冲区块的哈希值 id，用于确定对应的哈希桶
  // b->blockno 是缓冲区块 b 对应的磁盘块编号，用来标识该缓冲区块缓存了磁盘上的哪个块的数据
  int id = myhash(b->blockno);

  // 获取与该哈希桶关联的锁，以确保对缓冲区链表的安全访问
  acquire(&bcache.lks[id]);

  // ‘refcnt’ 记录了有多少线程或操作在使用这个缓冲区块
  // 减少缓冲区块的引用计数
  b->refcnt--;

  if (b->refcnt == 0) {
    // 不再采用原先淘汰表头元素的 LRU 算法了，取而代之的是基于时间戳的 LRU 算法。这里的时间戳，就是 kernel/trap.c:ticks
    // 更新 b->lastuse 为当前的时间 ticks
    b->lastuse = ticks;
  }
  
  // 释放与哈希桶关联的锁，允许其他线程访问该桶中的缓冲区块
  release(&bcache.lks[id]);
}

// 两个函数 bpin 和 bunpin，用于管理缓冲区块 b 的引用计数 refcnt

// 增加缓冲区块 b 的引用计数 refcnt
void bpin(struct buf *b) {
  int id = myhash(b->blockno);
  acquire(&bcache.lks[id]);
  b->refcnt++;
  release(&bcache.lks[id]);
}

void bunpin(struct buf *b) {
  int id = myhash(b->blockno);
  acquire(&bcache.lks[id]);
  b->refcnt--;
  release(&bcache.lks[id]);
}




