#include <snes.hpp>

#define CX4_CPP
namespace SNES {

#include "bus.cpp"
#include "memory.cpp"
#include "registers.cpp"
#include "instructions.cpp"
#include "data.cpp"
#include "serialization.cpp"

Cx4 cx4;

void Cx4::Enter() { cx4.enter(); }

void Cx4::enter() {
  while (true) {
    if (scheduler.sync == Scheduler::SynchronizeMode::All) {
      scheduler.exit(Scheduler::ExitReason::SynchronizeEvent);
    }

    bool wasBusy = busy();

    if (mmio.dma) {
      for (unsigned n = 0; n < mmio.dmaLength; n++) {
        uint8 data = cx4bus.read(mmio.dmaSource + n);
        add_clocks(2 + speed(mmio.dmaSource + n) + speed(mmio.dmaTarget + n));
        cx4bus.write(mmio.dmaTarget + n, data);
      }
      mmio.dma = false;
    }

    if (mmio.cacheLoading) {
      load_page(mmio.cachePreload, mmio.pageNumber);
    }
    
    if (!regs.halt) {
      if (cache[regs.cachePage].pageNumber != regs.pc >> 8) {
        // cache new program page
        load_page(regs.cachePage, regs.pc >> 8);
      }
    
      opcode = cache[regs.cachePage].data[regs.pc & 0xff];
      nextpc();
      instruction();
    }
    
    regs.irqPending |= wasBusy && !busy();
    add_clocks(1);
  }
}

void Cx4::add_clocks(unsigned clocks) {
  if(regs.rwbustime) {
    regs.rwbustime -= min(clocks, regs.rwbustime);
    if(regs.rwbustime == 0) {
      if (regs.writebus)
        cx4bus.write(regs.rwbusaddr, regs.writebusdata);
      else
        regs.busdata = cx4bus.read(regs.rwbusaddr);
    }
  }
  
  step(clocks);
  synchronize_cpu();
  
  while (mmio.suspend) {
    step(1);
    synchronize_cpu();
    
    if (mmio.suspendCycles && !--mmio.suspendCycles)
      mmio.suspend = false;
  }
  
  if (regs.irqPending && !mmio.irqDisable) {
    cpu.regs.irq = 1;
  }
}

void Cx4::nextpc() {
  regs.pc++;
  if ((regs.pc & 0xff) == 0) {
    // continue to next page or stop
    if (!regs.cachePage) {
      regs.pc = regs.p << 8; // ?
      regs.cachePage = 1;
    } else {
      regs.halt = true;
    }
  }
}

void Cx4::change_page() {
  bool otherPage = regs.cachePage ^ 1;
  uint16 programPage = regs.pc >> 8;
  
  if (cache[regs.cachePage].pageNumber == programPage) {
    // still in same page
  }
  // another locked page can be executed if it's the correct program page
  else if (!cache[otherPage].lock || cache[otherPage].pageNumber == programPage) {
    regs.cachePage = otherPage;
  }
  // if both pages are locked (and invalid), halt
  else if (cache[regs.cachePage].lock && cache[regs.cachePage].pageNumber != programPage) {
    regs.halt = true;
  }
}

void Cx4::load_page(uint8 cachePage, uint16 programPage) {
  uint24 addr = mmio.programOffset + (programPage << 9);
  
  // used for busy flag
  mmio.cacheLoading = true;
  
  for (unsigned i = 0; i < 256; i++) {
    cache[cachePage].data[i] = cx4bus.read(addr); // | (cx4bus.read(addr++) << 8);
    add_clocks(1 + speed(addr++));
    cache[cachePage].data[i] |= (cx4bus.read(addr) << 8);
    add_clocks(1 + speed(addr++));
  }
  
  cache[cachePage].pageNumber = programPage;
  mmio.cacheLoading = false;
}

void Cx4::init() {
}

void Cx4::enable() {
}

void Cx4::power() {
  cx4bus.init();
  reset();
}

void Cx4::reset() {
  create(Cx4::Enter, frequency);
  
  memset(dataRAM, 0, sizeof(dataRAM));
  
  regs.halt = true;
  regs.cachePage = 0;
  regs.irqPending = false;
  regs.rwbustime = 0;

  regs.n = 0;
  regs.z = 0;
  regs.c = 0;
  
  mmio.suspend = false;
  mmio.cacheLoading = false;

  mmio.dmaSource = 0x000000;
  mmio.dmaLength = 0x0000;
  mmio.dmaTarget = 0x000000;
  mmio.cachePreload = 0x00;
  mmio.programOffset = 0x000000;
  mmio.pageNumber = 0x0000;
  mmio.programCounter = 0x00;
  mmio.romSpeed = 0x3;
  mmio.ramSpeed = 0x3;
  mmio.irqDisable = 0x00;
  mmio.r1f52 = 0x01;
  
  for (auto& cachePage : cache) {
    cachePage.lock = false;
    cachePage.pageNumber = 0xffff;
    memset(cachePage.data, 0, sizeof(cachePage.data));
  }
}

}
