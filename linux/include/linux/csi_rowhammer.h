#ifndef CSI_ROWHAMMER_H
#define CSI_ROWHAMMER_H

void handle_memory_corruption(struct pt_regs *regs, unsigned long address);

void csi_stii(uintptr_t physical_address, uint64_t data);

void csi_rowhammer_init(void);

#endif