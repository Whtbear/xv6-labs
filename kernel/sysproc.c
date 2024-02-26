#include "types.h"
#include "riscv.h"
#include "param.h"
#include "defs.h"
#include "date.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;


  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}


#ifdef LAB_PGTBL
extern pte_t * walk(pagetable_t, uint64, int);

uint64
sys_pgaccess(void)
{
  // lab pgtbl: your code here.
  // 代码修改，检测已访问的页面
  uint64 bitmask = 0;//输出掩码位
  
  uint64 user_va;//起始虚拟地址
  uint64 bitmask_va;//位掩码的虚拟地址
  int scanpages;//要检查的页数
  if(argint(1, &scanpages)) return -1;//得到检查的页数
  if(scanpages > MAX_SCAN)
    return -1;
    
  if(argaddr(0, &user_va) < 0)
    return -1;
    
  if(argaddr(2, &bitmask_va) < 0)
    return -1;
  
  pte_t* pte;  
  
  //从开始地址逐页判断PTE_A的置位
  for(int i = 0;i < scanpages; user_va += PGSIZE, i++){//调用walk函数，直接在用户页表中找到最低一级PTE
    if((pte = walk(myproc()->pagetable, user_va,0)) == 0)
      return -1;
    if(*pte & PTE_A){
      bitmask |= 1 << i;//设置对应位，每页使用一个比特来标记mask
      *pte &= ~PTE_A; //这里记得要还原PTE_A
    }
  }
  
  copyout(myproc()->pagetable, bitmask_va, (char*)&bitmask, sizeof(bitmask));
  
  return 0;
}
#endif

uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}
