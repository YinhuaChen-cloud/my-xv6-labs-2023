#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

void main();
void timerinit();

// entry.S needs one stack per CPU.
__attribute__ ((aligned (16))) char stack0[4096 * NCPU];

// a scratch area per CPU for machine-mode timer interrupts.
uint64 timer_scratch[NCPU][5];

// assembly code in kernelvec.S for machine-mode timer interrupt.
extern void timervec();

// entry.S jumps here in machine mode on stack0.
void
start()
{
  // set M Previous Privilege mode to Supervisor, for mret.
  // 读取 mstatus 寄存器，把 MPP 那两个 bits 清零，再设置为 S mode
  // 当 mret 发生时，机器权限会被设置成 MPP 设置的权限
  unsigned long x = r_mstatus();
  x &= ~MSTATUS_MPP_MASK;
  x |= MSTATUS_MPP_S;
  w_mstatus(x);

  // 把 mepc 的值设置为 main 函数的地址
  // mepc: 当低权级的代码遇到 exception 时，会把当前 PC 写入 mepc
  // 在 machine mode 处理完异常后，再调用 mret 返回 mepc 指向的地址
  // set M Exception Program Counter to main, for mret.
  // requires gcc -mcmodel=medany
  w_mepc((uint64)main);

  // disable paging for now.
  // 关闭 supervisor mode 的页表翻译
  w_satp(0);

  // delegate all interrupts and exceptions to supervisor mode.
  // 根据手册，默认情况下，机器所有的 trap 都在 M mode 完成。当然 M mode 也可以
  // 通过 mret 来把 handler 转到 S mode。但为了提高效率，通过设置 medeleg or mideleg 
  // 中的任意一个比特，就可以把 S mode 和 U mode 的 trap 转移到 S mode 去处理
  w_medeleg(0xffff);
  w_mideleg(0xffff);
  // 开启 supervisor 级别的外部中断、时钟中断、软件中断
  w_sie(r_sie() | SIE_SEIE | SIE_STIE | SIE_SSIE);

  // 这两行函数调用：把 0x0000_0000 ~ 0xFFFF_FFFF 内存段都设置成可读可写可执行
  // configure Physical Memory Protection to give supervisor mode
  // access to all of physical memory.
  // pmpaddr0 在 64 位下，bits(63, 54) 是 WIRI 置空
  w_pmpaddr0(0x3fffffffffffffull);
  // 对应地址区域可读、可写、可执行、顶部匹配模式
  // If TOR is selected, the associated address register forms the top of the address range, and the
  // preceding PMP address register forms the bottom of the address range.
  w_pmpcfg0(0xf);

  // 设置时钟中断 (TODO: 为什么时钟中断不会被分派给 supervisor mode)
  // ask for clock interrupts.
  timerinit();

  // 把 hartid 存入 tp 寄存器，thread pointer
  // keep each CPU's hartid in its tp register, for cpuid().
  int id = r_mhartid();
  w_tp(id);

  // 调用 mret，进入 supervisor mode，同时进入 main 函数
  // switch to supervisor mode and jump to main().
  asm volatile("mret");
}

// arrange to receive timer interrupts.
// they will arrive in machine mode at
// at timervec in kernelvec.S,
// which turns them into software interrupts for
// devintr() in trap.c.
void
timerinit()
{
  // each CPU has a separate source of timer interrupts.
  // 读取 mhartid
  int id = r_mhartid();

  // ask the CLINT for a timer interrupt.
  int interval = 1000000; // cycles; about 1/10th second in qemu.
  *(uint64*)CLINT_MTIMECMP(id) = *(uint64*)CLINT_MTIME + interval;

  // prepare information in scratch[] for timervec.
  // scratch[0..2] : space for timervec to save registers.
  // scratch[3] : address of CLINT MTIMECMP register.
  // scratch[4] : desired interval (in cycles) between timer interrupts.
  uint64 *scratch = &timer_scratch[id][0];
  scratch[3] = CLINT_MTIMECMP(id);
  scratch[4] = interval;
  // mscratch 寄存器用于机器模式下的程序临时保存某些数据。
  w_mscratch((uint64)scratch);

  // set the machine-mode trap handler.
  // mtvec = timervec，意思是，发生 machine mode 中断时，会跳转到 timervec
  w_mtvec((uint64)timervec);

  // enable machine-mode interrupts.
  // 开启机器模式下中断
  w_mstatus(r_mstatus() | MSTATUS_MIE);

  // enable machine-mode timer interrupts.
  // 开启机器模式下的时钟中断
  w_mie(r_mie() | MIE_MTIE);
}
