//
// simple PCI-Express initialization, only
// works for qemu and its e1000 card.
//

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

void
pci_init()
{
  // vm.c 中的 kvmmake 已经提前映射了 0x30000000L 和 0x40000000L
  // e1000 的寄存器映射在 0x40000000L
  // PCIe 的配置空间映射在 0x30000000L

  // we'll place the e1000 registers at this address.
  // vm.c maps this range.

  uint64 e1000_regs = 0x40000000L;

  // qemu -machine virt puts PCIe config space here.
  // vm.c maps this range.
  uint32  *ecam = (uint32 *) 0x30000000L;

  // 扫描 PCIe 配置空间，查找设备

  // look at each possible PCI device on bus 0.
  for(int dev = 0; dev < 32; dev++){
    int bus = 0;
    int func = 0;
    int offset = 0;
    uint32 off = (bus << 16) | (dev << 11) | (func << 8) | (offset);
    volatile uint32 *base = ecam + off;
    uint32 id = base[0];
    
    // 如果找到了 e1000
    // 100e:8086 is an e1000
    if(id == 0x100e8086){
      // 设置 e1000 的属性 (这里要看 e1000 的手册)
      // command and status register.
      // bit 0 : I/O access enable
      // bit 1 : memory access enable
      // bit 2 : enable mastering
      base[1] = 7;
      __sync_synchronize();

      // 对 e1000 的配置空间做一些操作，具体是啥咱不关心
      for(int i = 0; i < 6; i++){
        uint32 old = base[4+i];

        // writing all 1's to the BAR causes it to be
        // replaced with its size.
        base[4+i] = 0xffffffff;
        __sync_synchronize();

        base[4+i] = old;
      }

      // NOTE: 这个很有趣！把 e1000 的寄存器映射在 0x40000000
      // tell the e1000 to reveal its registers at
      // physical address 0x40000000.
      base[4+0] = e1000_regs;

      // 进一步初始化
      e1000_init((uint32*)e1000_regs);
    }
  }
}
