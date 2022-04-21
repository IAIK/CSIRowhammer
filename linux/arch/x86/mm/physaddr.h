/* SPDX-License-Identifier: GPL-2.0 */
#include <asm/processor.h>
#define UPPER_MEM_FLAG ((1UL) << 56)
static inline int phys_addr_valid(resource_size_t addr)
{
#ifdef CONFIG_PHYS_ADDR_T_64BIT
	return !(addr >> boot_cpu_data.x86_phys_bits) || ((addr & UPPER_MEM_FLAG) != 0);
#else
	return 1;
#endif
}
