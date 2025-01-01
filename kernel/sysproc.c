#include "types.h"
#include "riscv.h"
#include "param.h"
#include "defs.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
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
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
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


  argint(0, &n);
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

#ifdef LAB_PGTBL
int
sys_pgaccess(void)
{
  // lab pgtbl: your code here.
  // Be sure to clear PTE_A after checking if it is set. 
  // Otherwise, it won't be possible to determine if the page 
  // was accessed since the last time pgaccess() was called 
  // (i.e., the bit will be set forever).

  // read args
  uint64 va;
  int npage;
  uint64 abits_addr;

  argaddr(0, &va);
  argint(1, &npage);
  argaddr(2, &abits_addr);
    
  // the number of scanned pages cannot be more than 64
  if(npage > 64) {
    panic("npage > 64!");
  }

  // temporary buffer for output bitmask
  uint64 aux_abits = 0;
  
  // update temporary buffer
  // CYHNOTE: the first page corresponds to the least significant bit
  // get the current process
  struct proc* p = myproc();
  for (int i = 0; i < npage; i++) {
    pte_t *pte = walk(p->pagetable, va + i*PGSIZE, 0);
    if(0 == pte)
      panic("0 == pte");
    if(!(*pte & PTE_V))
      panic("!(*pte & PTE_V)");
    if(*pte & PTE_A) {
      aux_abits |= (1L << i);
      // clear PTE_A after checking if it is set.
      *pte &= ~(PTE_A);
    }
  }

  // copy temporary buffer to the user
  if(copyout(p->pagetable, abits_addr, (char *)&aux_abits, npage/8) < 0) {
    panic("copyout");
  }

  return 0;
}
#endif

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
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
