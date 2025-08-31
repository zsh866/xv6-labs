#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "fcntl.h"
#include "proc.h"



struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

void
trapinit(void)
{
  initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
static int mmap_handler(uint64 va, uint64 scause);

void
usertrap(void)
{
  int which_dev = 0;
  struct proc *p = myproc();

  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(), not usertrap()
  w_stvec((uint64)kernelvec);

  // save user program counter.
  p->trapframe->epc = r_sepc();

  uint64 scause = r_scause();

  if(scause == 8){
    // system call
    if(p->killed)
      exit(-1);
    p->trapframe->epc += 4;
    intr_on();
    syscall();
  } else if((which_dev = devintr()) != 0){
    // ok
  } else if(scause == 13 || scause == 15){ // load/store page fault
#ifdef LAB_MMAP
    uint64 fault_va = r_stval();

    // 只允许在用户地址空间有效范围内（避免越界到内核/无效区域）
    // 也常见做法是确保在 [stack, p->sz) 之间；你可按实验手册约束。
    if (fault_va < p->sz && fault_va >= p->trapframe->sp){
      if(mmap_handler(fault_va, scause) != 0)
        p->killed = 1;
    } else {
      p->killed = 1;
    }
#else
    p->killed = 1;
#endif
  } else {
    printf("usertrap(): unexpected scause %p pid=%d\n", scause, p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    p->killed = 1;
  }

  if(p->killed)
    exit(-1);

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2)
    yield();

  usertrapret();
}

// 处理 mmap 的缺页：分配物理页、从文件读入并映射。
static int
mmap_handler(uint64 va, uint64 scause)
{
  struct proc *p = myproc();

  // 找到包含此 VA 的 VMA
  int idx = -1;
  for (int i = 0; i < NVMA; i++) {
    if (p->vma[i].used) {
      uint64 start = p->vma[i].addr;
      uint64 end   = start + p->vma[i].len; // 半开区间 [start, end)
      if (va >= start && va < end) {
        idx = i;
        break;
      }
    }
  }
  if (idx < 0)
    return -1;

  struct vm_area *vma = &p->vma[idx];
  struct file *f = vma->vfile;

  // 访问权限基本检查（简化版）
  if (scause == 13 && f->readable == 0)   return -1; // load fault但文件不可读
  if (scause == 15 && ( (vma->prot & PROT_WRITE) == 0 || f->writable == 0))
    return -1; // store fault但VMA不写或文件不可写（MAP_SHARED 时尤为关键）

  // 目标页对齐
  uint64 va0 = PGROUNDDOWN(va);

  // 分配物理页
  void *pa = kalloc();
  if (pa == 0)
    return -1;
  memset(pa, 0, PGSIZE);

  // 计算从文件读取的偏移：VMA起点对应文件 offset，VA 的页内偏移按 (va0 - vma->addr)
  int file_off = vma->offset + (int)(va0 - vma->addr);

  // 从文件读入至物理页
  ilock(f->ip);
  int n = readi(f->ip, 0, (uint64)pa, file_off, PGSIZE);
  iunlock(f->ip);

  // readi 可能返回 < PGSIZE（到达文件尾），0 也算“读不到内容”，但通常允许；
  // 规范做法是：保留其余为 0（我们已 memset 过），仅当需要严格拒绝时返回 -1。
  // 这里按实验常规：n 可以是 0..PGSIZE 都接受。
  if (n < 0) {
    kfree(pa);
    return -1;
  }

  // 组装 PTE 权限
  int pte_flags = PTE_U;
  if (vma->prot & PROT_READ)  pte_flags |= PTE_R;
  if (vma->prot & PROT_WRITE) pte_flags |= PTE_W;
  if (vma->prot & PROT_EXEC)  pte_flags |= PTE_X;

  // 建立映射
  if (mappages(p->pagetable, va0, PGSIZE, (uint64)pa, pte_flags) != 0) {
    kfree(pa);
    return -1;
  }

  return 0;
}
/* void
usertrap(void)
{
  int which_dev = 0;

  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();
  
  // save user program counter.
  p->trapframe->epc = r_sepc();
  
  if(r_scause() == 8){
    // system call

    if(killed(p))
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    p->trapframe->epc += 4;

    // an interrupt will change sepc, scause, and sstatus,
    // so enable only now that we're done with those registers.
    intr_on();

    syscall();
  } else if((which_dev = devintr()) != 0){
    // ok
  } else {
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    setkilled(p);
  }

  if(killed(p))
    exit(-1);

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2)
    yield();

  usertrapret();
}
 */
//
// return to user space
//
void
usertrapret(void)
{
  struct proc *p = myproc();

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct.
  intr_off();

  // send syscalls, interrupts, and exceptions to uservec in trampoline.S
  uint64 trampoline_uservec = TRAMPOLINE + (uservec - trampoline);
  w_stvec(trampoline_uservec);

  // set up trapframe values that uservec will need when
  // the process next traps into the kernel.
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.
  
  // set S Previous Privilege mode to User.
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  w_sepc(p->trapframe->epc);

  // tell trampoline.S the user page table to switch to.
  uint64 satp = MAKE_SATP(p->pagetable);

  // jump to userret in trampoline.S at the top of memory, which 
  // switches to the user page table, restores user registers,
  // and switches to user mode with sret.
  uint64 trampoline_userret = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64))trampoline_userret)(satp);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void 
kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if((which_dev = devintr()) == 0){
    printf("scause %p\n", scause);
    printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
    panic("kerneltrap");
  }

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING)
    yield();

  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void
clockintr()
{
  acquire(&tickslock);
  ticks++;
  wakeup(&ticks);
  release(&tickslock);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int
devintr()
{
  uint64 scause = r_scause();

  if((scause & 0x8000000000000000L) &&
     (scause & 0xff) == 9){
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim();

    if(irq == UART0_IRQ){
      uartintr();
    } else if(irq == VIRTIO0_IRQ){
      virtio_disk_intr();
    } else if(irq){
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if(irq)
      plic_complete(irq);

    return 1;
  } else if(scause == 0x8000000000000001L){
    // software interrupt from a machine-mode timer interrupt,
    // forwarded by timervec in kernelvec.S.

    if(cpuid() == 0){
      clockintr();
    }
    
    // acknowledge the software interrupt by clearing
    // the SSIP bit in sip.
    w_sip(r_sip() & ~2);

    return 2;
  } else {
    return 0;
  }
}

