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

struct reference{ //引用计数结构体
  struct spinlock lock;
  int cnt[PHYSTOP / PGSIZE]; //引用计数，PHYSTOP物理内存为终止地址
} ref;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  //初始化引用中的自旋锁
  initlock(&ref.lock, "ref");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE){
    ref.cnt[(uint64)p / PGSIZE] = 1;//防止将计数减为负数
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
  // 这里当引用计数为0再回收空间
  // Fill with junk to catch dangling refs.
  acquire(&ref.lock);
  if(--ref.cnt[(uint64)pa / PGSIZE] == 0){//引用计数为0,回收空间
    release(&ref.lock);
    memset(pa, 1, PGSIZE);

    r = (struct run*)pa;

    acquire(&kmem.lock);
    r->next = kmem.freelist;
    kmem.freelist = r;
    release(&kmem.lock);
  }
  else{//单纯减1
    release(&ref.lock);
  }
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
    acquire(&ref.lock);//需要加锁，避免计数器引用初始化时被打断
    ref.cnt[(uint64)r / PGSIZE] = 1; // 将分配的页面的引用计数初始化为1
    release(&ref.lock);
    }
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

int  //判断一个页面是否为COW页
is_cow(pagetable_t pagetable, uint64 va){
  if(va >= MAXVA){
    return -1;
  }
  pte_t* pte = walk(pagetable, va, 0); //获得对应的PTE入口
  if(pte == 0)  
    return -1;
  if((*pte & PTE_V) == 0)  //页面要有效
    return -1;
  return (*pte & PTE_COW ? 0 : -1);  //查看是否为COW页
}

void*
cowalloc(pagetable_t pagetable, uint64 va){//指定页表和虚拟地址，返回分配后va对应的pa
  if(va % PGSIZE != 0)
    return 0;
  uint64 pa = walkaddr(pagetable, va);//获得相应的物理地址
  if(pa == 0)
    return 0;
    
  pte_t* pte = walk(pagetable, va, 0); // 获取对应的PTE
  
  if(ref.cnt[(uint64)pa / PGSIZE] == 1){//此处是只剩一个进程对此物理地址存在引用，直接修改PTE
    *pte |= PTE_W;//如果pte原来可写，则还原可写；若原来不可写，则不可写
    *pte &= ~PTE_COW;//将COW标记还原
    return (void*)pa;
  }
  else{
    //存在子进程对物理内存进行引用
    //分配新的页面，拷贝旧页面内容
    char* mem = kalloc();
    if (mem == 0)
      return 0;
      
    //复制旧页面到新页面
    memmove(mem, (char*)pa, PGSIZE);
    
    //清除PTE_V, 以便添加新的映射,否则映射到原来页面
    *pte &= ~PTE_V;
    
    //为新页面添加映射
    if(mappages(pagetable, va, PGSIZE, (uint64)mem, (PTE_FLAGS(*pte) | PTE_W) & ~PTE_COW) != 0){//发生错误释放当前内存
      kfree(mem);
      *pte |= PTE_V;//恢复PTE_V
      return 0;
    }
    
    //这里是往低地址处对齐4k页面的物理地址
    //将原来的物理内存引用计数减1
    kfree((char*)PGROUNDDOWN(pa));
    return mem;
  }
  
}

int
kaddrefcnt(void* pa){
  //这里增加对应物理内存的引用计数，需要加锁
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)//要判断pa是否对齐，是否超过了物理内存大小，end[]为kernel ld定义的内核后第一个地址
    return -1;
  acquire(&ref.lock);
  ++ref.cnt[(uint64)pa / PGSIZE];
  release(&ref.lock);
  return 0;
}

