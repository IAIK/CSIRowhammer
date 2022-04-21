#include <linux/context_tracking.h> /* exception_enter(), ...	*/
#include <linux/extable.h> /* search_exception_tables	*/
#include <linux/hugetlb.h> /* hstate_index_to_shift	*/
#include <linux/kdebug.h> /* oops_begin/end, ...		*/
#include <linux/kprobes.h> /* NOKPROBE_SYMBOL, ...		*/
#include <linux/memblock.h> /* max_low_pfn			*/
#include <linux/mm_types.h>
#include <linux/mmiotrace.h> /* kmmio_handler, ...		*/
#include <linux/perf_event.h> /* perf_sw_event		*/
#include <linux/prefetch.h> /* prefetchw			*/
#include <linux/sched.h> /* test_thread_flag(), ...	*/
#include <linux/sched/task_stack.h> /* task_stack_*(), ...		*/
#include <linux/types.h>
#include <linux/uaccess.h> /* faulthandler_disabled()	*/

#include <asm/cpu_entry_area.h> /* exception stack		*/
#include <asm/cpufeature.h> /* boot_cpu_has, ...		*/
#include <asm/desc.h> /* store_idt(), ...		*/
#include <asm/fixmap.h> /* VSYSCALL_ADDR		*/
#include <asm/kvm_para.h> /* kvm_handle_async_pf		*/
#include <asm/mmu_context.h> /* vma_pkey()			*/
#include <asm/pgtable_areas.h> /* VMALLOC_START, ...		*/
#include <asm/traps.h> /* dotraplinkage, ...		*/
#include <asm/vm86.h> /* struct vm86			*/
#include <asm/vsyscall.h> /* emulate_vsyscall		*/

typedef u8  uint8_t;
typedef u16 uint16_t;
typedef u32 uint32_t;
typedef u64 uint64_t;
typedef s8  int8_t;
typedef s16 int16_t;
typedef s32 int32_t;
typedef s64 int64_t;

typedef unsigned long uintptr_t;


#define DATA_SIZE 512
#define DATA_SIZE_BYTE_LOG 6    // log2(DATA_SIZE / 8)
#define MAX_FLIPS 7
#define CONST_NUM_PARITY_BITS 8
#define PARITY_BLOCK_SIZE (DATA_SIZE / CONST_NUM_PARITY_BITS)

#define FLIPBIT(data,i) ((data[(i) / (8 * sizeof(*data))] ^= (1 << ((i) % (8 * sizeof(*data))))))


#define NOP() asm volatile("nop")
#define NNOP() NOP();NOP();NOP();NOP();NOP();NOP();NOP();NOP();NOP();NOP();
#define NNNOP() NNOP();NNOP();NNOP();NNOP();NNOP();NNOP();NNOP();NNOP();NNOP();NNOP();
#define NNNNOP() NNNOP();NNNOP();NNNOP();NNNOP();NNNOP();NNNOP();NNNOP();NNNOP();NNNOP();NNNOP();


inline uint64_t csi_mac(uint64_t *value, uint64_t address);
uint64_t csi_load(uintptr_t corruption_address, uint64_t *corrupted_data, uint64_t *integrity_information);
uint64_t csi_xchg(uintptr_t corruption_address, uint64_t *original_data, uint64_t *new_data);
void csi_stii(uintptr_t physical_address, uint64_t data);
int correction_as_a_search(uint64_t data[], uint64_t integrity_information, uintptr_t address);
int advanced_correction(uint64_t data[], uint64_t integrity_information, uintptr_t address);
void exc_csi_correction(struct pt_regs *regs, unsigned long error_code);  // the exception handler


__attribute__((optimize("align-functions=4096")))
void handle_memory_corruption(struct pt_regs *regs, unsigned long address)
{
	volatile unsigned long cr3;

	uint64_t integrity_information;
	irqentry_state_t state;
	uint64_t value[8] __attribute__ ((aligned (512)));
	uint64_t oldvalue[8] __attribute__ ((aligned (512)));
	
	cr3 = __read_cr3();

	csi_load(address, value, &integrity_information);

	// store the corrupted value for the exchange at the end
	memcpy(oldvalue, value, 64);

	// check if nesting bit is set
	if (cr3 & (1ULL << 63)) {
		// Enable interrupts
		if (user_mode(regs)) {
			local_irq_enable();
		} else {
			if (regs->flags & X86_EFLAGS_IF)
				local_irq_enable();
		}

		printk(KERN_INFO "CSI:Rowhammer correction with nesting bit set\n");
		
		// perform correction by search
		if (!correction_as_a_search(value, integrity_information, address)) {
			panic("Nested correction unsuccessful!");
		}

		// the correction was successful
		csi_xchg(address, oldvalue, value);

		// return from interrupt
		local_irq_disable();
		irqentry_exit(regs, state);
		return;
	}
	else {
		// set nesting bit
		set_bit(63, &cr3);
		write_cr3(cr3);

		// Enable interrupts
		if (user_mode(regs)) {
			local_irq_enable();
		} else {
			if (regs->flags & X86_EFLAGS_IF)
				local_irq_enable();
		}
		
		printk(KERN_INFO "CSI:Rowhammer correction with nesting bit not set\n");

		// TODO advanced correction	
		// perform correction by search
		if (!advanced_correction(value, integrity_information, address)) {
			panic("Correction unsuccessful!");
		}

		// the correction was successful
		csi_xchg(address, oldvalue, value);

		// clear nesting bit, we dont' want to be interrupted while doing this
		local_irq_disable();
		cr3 = __read_cr3();
		cr3 &= ~(1ULL << 63);
		write_cr3(cr3);

		// return from interrupt
		irqentry_exit(regs, state);
		return;
	}
}


uint64_t csi_mac(uint64_t *value, uint64_t address)
{
	uint64_t mac = 0;
	
	asm volatile(
		"vmovdqu64 %1,%%zmm0 											\n\t"
		"mov %2,%%rax															\n\t"
		".byte 0x62, 0xf1, 0xfe, 0x48, 0x6e, 0xc1	\n\t"
		"nop																			\n\t"		// without the nops the kernel does not
		"nop																			\n\t"		// compile because objtool is confused
		"nop																			\n\t"		// by our custom instruction
		"nop																			\n\t"
		"nop																			\n\t"
		"nop																			\n\t"
		"mov %%rax,%0 														\n\t"
		: "=m"(mac)
		: "m"(*value), "m"(address)
		: );
	
	return mac;
}


uint64_t csi_load(uintptr_t corruption_address, 
									uint64_t *corrupted_data,
									uint64_t *integrity_information)
{
	asm volatile(
		"mov %2,%%rax															\n\t"
		".byte 0x62, 0xf1, 0xfe, 0x48, 0x6d, 0x00	\n\t"
		"nop																			\n\t"		// without the nops the kernel does not
		"nop																			\n\t"		// compile because objtool is confused
		"nop																			\n\t"		// by our custom instruction
		"nop																			\n\t"
		"nop																			\n\t"
		"vmovdqu64 %%zmm0,%0											\n\t"
		"mov %%rax,%1															\n\t"
		: "=m"(*corrupted_data), "=m"(*integrity_information)
		: "m"(corruption_address)
		: "memory");

	return 0;	
}


uint64_t csi_xchg(uintptr_t corruption_address, 
									uint64_t *original_data,
									uint64_t *new_data)
{
	asm volatile(
		"mov %0,%%rax															\n\t"
		"vmovdqu64 %1,%%zmm0											\n\t"
		"vmovdqu64 %2,%%zmm1											\n\t"
		".byte 0x62, 0xf1, 0xfe, 0x48, 0x6c, 0x00	\n\t"
		"nop																			\n\t"		// without the nops the kernel does not
		"nop																			\n\t"		// compile because objtool is confused
		"nop																			\n\t"		// by our custom instruction
		"nop																			\n\t"
		"nop																			\n\t"
		: 
		: "m"(corruption_address), "m"(*original_data), "m"(*new_data)
		: "memory");

	return 0;	
}


void csi_stii(uintptr_t physical_address, uint64_t data)
{
	asm volatile(
		"mov %0,%%rax			\n\t"
		"mov %1,%%rsi			\n\t"
		".byte 0x8c, 0x30 \n\t"
		:
		: "m"(physical_address), "m"(data)
		: );
}


inline int get_parity(uint8_t d) {
	int p = 0;
	int i;
	
	for (i = 0; i < 8; i++) {
		p ^= (d & 1);
		d >>= 1;
	}

	return p;
}


inline int popcount(uint64_t d) {
	int c = 0;
	int i;

	for (i = 0; i < 64; i++) {
		c += (d & 1);
		d >>= 1;
	}

	return c;
}


inline uint8_t compute_parity(uint8_t data[]) {
	uint8_t parity = 0;
	int i, j, p, di = 0;
	for (i = 0; i < CONST_NUM_PARITY_BITS; i++) {
		p = 0;
		for (j = 0; j < DATA_SIZE / (8 * CONST_NUM_PARITY_BITS); j++, di++) {
			p ^= get_parity(data[di]);
		}
		parity |= (p << i);
	}
	return parity;
}


/**
 * @brief This is the correction as a search function.
 *        It takes the data and flips bits until it finds the flips to
 *        get correct_data. It uses the parity bits to guide the serach.
 * 
 * @param data corrupted data as an array of 32 bit integers
 * @param correct_data Correct data because we use a simple memcpy for the
 *                     check if the correction is successful.
 *                     In the real code, the MAC is supplied
 * @param parity The parity bits that are read from the DRAM
 * 
 * @return number of compares (MAC computations) performed for statistics 
 */
//__attribute__((optimize("O0"))) 
int correction_as_a_search(uint64_t data[], const uint64_t integrity_information, const uintptr_t address) {
	unsigned int i, j, p;

	uint64_t mac, computed_mac;
	int d = 3;  // number of bits that can be different for approximate equality

	uint8_t parity, data_parity, parity_wrong;
	int no_parity_flip_min_flips;
	int min_flips, double_flips_addition;
	int new_min_flips;

	int num_wrong_parity_bits;
	unsigned int prob_flip_positions[8];
	int parity_flip;

	unsigned int parity_block_size = PARITY_BLOCK_SIZE;

	unsigned int pb[4];    // parity bit block
	unsigned int bit[8];
	// search start/end bits can be any parity block or bit value
	unsigned int *ssb[8];
	unsigned int *seb[8];

	uint64_t *data_parity_block[8];
	uint64_t flip_mask[8];


	// We use a great number of gotos for the best possible performance.
	// There is one large nested bit permutation loop and these jump labels
	// define where to jump to in this loop to correct the number of bit
	// flips currently tried.
	// This safes a lot of space without loosing much performance.
	void *lbl_flip;
	void *lbl_flip_break[9], *lbl_pb_break[5];

	void *LBL_FLIP[9] = {
		NULL,            // let the array start with 1
		&&LBL_1_FLIP,
		&&LBL_2_FLIP,
		&&LBL_3_FLIP,
		&&LBL_4_FLIP,
		&&LBL_5_FLIP,
		&&LBL_6_FLIP,
		&&LBL_7_FLIP,
		&&LBL_8_FLIP
	};
	void *LBL_PB[5] = {
		NULL,
		&&LBL_1_PB,
		&&LBL_2_PB,
		&&LBL_3_PB,
		&&LBL_4_PB
	};


	num_wrong_parity_bits = 0;
	double_flips_addition = 0;
	no_parity_flip_min_flips = 0;


	mac = integrity_information & ((1ULL << 56) - 1);
	parity = integrity_information >> 56;


	data_parity = compute_parity((uint8_t*)data);
	
	parity_wrong = data_parity ^ parity;
	min_flips = popcount(parity_wrong);

	// Get the possible bit flip locations from the parity bits
	for (p = 0; p < 8; p++) {
		if ((parity_wrong >> p) & 1) {
			prob_flip_positions[num_wrong_parity_bits] = p * PARITY_BLOCK_SIZE;
			num_wrong_parity_bits++;
		}
	}
 
	if (min_flips < 2) {
		// maybe there is no flip
		computed_mac = csi_mac(data, address);
		if (popcount(mac ^ computed_mac) <= d) {
			return 1;
		}

		// there is a double flip in at least one parity bit input
		if (min_flips == 0) {
			no_parity_flip_min_flips = 2;
			double_flips_addition = 2;
		}
	}
	
	// Here starts the correction as a search
	no_parity_flip_min_flips = min_flips + double_flips_addition;

	while (1) {
		// -1 means no parity flip
		// 0 first parity bit, 1 second parity bit, ...
		for (parity_flip = -1; parity_flip < CONST_NUM_PARITY_BITS; parity_flip++) {

			lbl_pb_break[1] = &&LBL_1_PB_BREAK;
			lbl_pb_break[2] = &&LBL_2_PB_BREAK;
			lbl_pb_break[3] = &&LBL_3_PB_BREAK;
			lbl_pb_break[4] = &&LBL_4_PB_BREAK;
			lbl_flip_break[1] = &&LBL_1_FLIP_BREAK;
			lbl_flip_break[2] = &&LBL_2_FLIP_BREAK;
			lbl_flip_break[3] = &&LBL_3_FLIP_BREAK;
			lbl_flip_break[4] = &&LBL_4_FLIP_BREAK;
			lbl_flip_break[5] = &&LBL_5_FLIP_BREAK;
			lbl_flip_break[6] = &&LBL_6_FLIP_BREAK;
			lbl_flip_break[7] = &&LBL_7_FLIP_BREAK;
			lbl_flip_break[8] = &&LBL_8_FLIP_BREAK;

			pb[0] = CONST_NUM_PARITY_BITS - 1;
			pb[1] = CONST_NUM_PARITY_BITS - 1;
			pb[2] = CONST_NUM_PARITY_BITS - 1;
			pb[3] = CONST_NUM_PARITY_BITS - 1;


			if (parity_flip != -1) {
				parity ^= (1 << parity_flip);
			}
			
			parity_wrong = data_parity ^ parity;
			new_min_flips = popcount(parity_wrong) + double_flips_addition;


			// Only try parity flip if it decreases the number of expected flips 
			if (parity_flip != -1 && new_min_flips >= no_parity_flip_min_flips) {
				if (double_flips_addition != 0)
					new_min_flips -= 2;
				else
					goto LBL_CONTINUE;
			}

			if (new_min_flips == 0)
				goto LBL_CONTINUE;

			min_flips = new_min_flips;

			num_wrong_parity_bits = 0;
			for (p = 0; p < 8; p++) {
				if ((parity_wrong >> p) & 1) {
					prob_flip_positions[num_wrong_parity_bits] = p;
					num_wrong_parity_bits++;
				}
			}

			// set the maximum allowed number of different bits in the MAC
			if (min_flips > 4) {
				d = 7 - min_flips;
			}
			// too many bitflips, we give up
			if (min_flips > 7) {
				return 0;
			}

			lbl_flip = LBL_FLIP[min_flips];

			// set the start and end values for the bit values
			// and the jump labels
			for (i = 0; i < num_wrong_parity_bits; i++) {
				ssb[i] = &(prob_flip_positions[i]);
				seb[i] = &parity_block_size;
			}

			if (num_wrong_parity_bits == min_flips) {
				lbl_flip_break[min_flips] = &&LBL_CONTINUE;

				// jump into the loop
				goto *lbl_flip;
			}
			else {
				unsigned int double_flips = (min_flips - num_wrong_parity_bits) / 2;

				lbl_flip_break[min_flips] = &&LBL_PB_CONTINUE;
				lbl_pb_break[double_flips] = &&LBL_CONTINUE;

				for (j = 0; j < double_flips; j++) {
					ssb[i]   = &(pb[j]);
					seb[i] = &(bit[i+1]);
					i++;
					ssb[i]   = &(pb[j]);
					seb[i] = &parity_block_size;
					i++;
				}

				// jump into the loop
				goto *LBL_PB[double_flips];
			}

			// The for outer PB loops are for parity bit input blocks with an even
			// number of flips and therefore no change in the parity bits.
			LBL_4_PB:
			for (pb[3] = 0; pb[3] < CONST_NUM_PARITY_BITS; pb[3]++) {

				LBL_3_PB:
				for (pb[2] = 0; pb[2] <= pb[3]; pb[2]++) {

					LBL_2_PB:
					for (pb[1] = 0; pb[1] <= pb[2]; pb[1]++) {

						LBL_1_PB:
						for (pb[0] = 0; pb[0] <= pb[1]; pb[0]++) {

							goto *lbl_flip;
							
							LBL_8_FLIP:
							for (bit[7] = 0, flip_mask[7] = 1, data_parity_block[7] = &data[*ssb[7]];
										bit[7] < *seb[7]; bit[7]++, flip_mask[7] <<= 1) {
								*data_parity_block[7] ^= flip_mask[7];

								LBL_7_FLIP:
								for (bit[6] = 0, flip_mask[6] = 1, data_parity_block[6] = &data[*ssb[6]];
											bit[6] < *seb[6]; bit[6]++, flip_mask[6] <<= 1) {
									*data_parity_block[6] ^= flip_mask[6];

									LBL_6_FLIP:
									for (bit[5] = 0, flip_mask[5] = 1, data_parity_block[5] = &data[*ssb[5]];
												bit[5] < *seb[5]; bit[5]++, flip_mask[5] <<= 1) {
										*data_parity_block[5] ^= flip_mask[5];

										LBL_5_FLIP:
										for (bit[4] = 0, flip_mask[4] = 1, data_parity_block[4] = &data[*ssb[4]];
													bit[4] < *seb[4]; bit[4]++, flip_mask[4] <<= 1) {
											*data_parity_block[4] ^= flip_mask[4];

											LBL_4_FLIP:
											for (bit[3] = 0, flip_mask[3] = 1, data_parity_block[3] = &data[*ssb[3]];
														bit[3] < *seb[3]; bit[3]++, flip_mask[3] <<= 1) {
												*data_parity_block[3] ^= flip_mask[3];
												
												LBL_3_FLIP:
												for (bit[2] = 0, flip_mask[2] = 1, data_parity_block[2] = &data[*ssb[2]];
															bit[2] < *seb[2]; bit[2]++, flip_mask[2] <<= 1) {
													*data_parity_block[2] ^= flip_mask[2];

													LBL_2_FLIP:
													for (bit[1] = 0, flip_mask[1] = 1, data_parity_block[1] = &data[*ssb[1]];
																bit[1] < *seb[1]; bit[1]++, flip_mask[1] <<= 1) {
														*data_parity_block[1] ^= flip_mask[1];

														// We also do the correction of single bit errors in this code
														// to check the correctness of the algorithm.
														LBL_1_FLIP:
														for (bit[0] = 0, flip_mask[0] = 1, data_parity_block[0] = &data[*ssb[0]];
																	bit[0] < *seb[0]; bit[0]++, flip_mask[0] <<= 1) {
															*data_parity_block[0] ^= flip_mask[0];

															computed_mac = csi_mac(data, address);
															if (popcount(mac ^ computed_mac) <= d) {
																return 1;
															}

															*data_parity_block[0] ^= flip_mask[0];
														}
														goto *lbl_flip_break[1];
														LBL_1_FLIP_BREAK:

														*data_parity_block[1] ^= flip_mask[1];
													}
													goto *lbl_flip_break[2];
													LBL_2_FLIP_BREAK:

													*data_parity_block[2] ^= flip_mask[2];
												}
												goto *lbl_flip_break[3];
												LBL_3_FLIP_BREAK:

												*data_parity_block[3] ^= flip_mask[3];
											}
											goto *lbl_flip_break[4];
											LBL_4_FLIP_BREAK:

											*data_parity_block[4] ^= flip_mask[4];
										}
										goto *lbl_flip_break[5];
										LBL_5_FLIP_BREAK:

										*data_parity_block[5] ^= flip_mask[5];
									}
									goto *lbl_flip_break[6];
									LBL_6_FLIP_BREAK:

									*data_parity_block[6] ^= flip_mask[6];
								}
								goto *lbl_flip_break[7];
								LBL_7_FLIP_BREAK:

								*data_parity_block[7] ^= flip_mask[7];
							}
							goto *lbl_flip_break[8];
							LBL_8_FLIP_BREAK:;
							LBL_PB_CONTINUE:;
						}
						goto *lbl_pb_break[1];
						LBL_1_PB_BREAK:;
					}
					goto *lbl_pb_break[2];
					LBL_2_PB_BREAK:;
				}
				goto *lbl_pb_break[3];
				LBL_3_PB_BREAK:;
			}
			goto *lbl_pb_break[4];
			LBL_4_PB_BREAK:;


			LBL_CONTINUE:
			if (parity_flip != -1) {
				parity ^= (1 << parity_flip);
			}
		}

		no_parity_flip_min_flips += 2;
		double_flips_addition += 2;
	}

	return 0;
}



__attribute__((optimize("align-functions=4096")))
int advanced_correction(uint64_t data[], uint64_t integrity_information, uintptr_t address)
{
	// Here the operating system can try different techniques to correct the bitflip
	// This PoC is used to proof the working fundamentals of CSI:Rowhammer, like the nesting bit.
	// By inducing an error into one of the NOPs the correction functionality of the 
	// nesting detection can be verified.

	NNNNOP();
	NNNNOP();
	NNNNOP();
	NNNNOP();

	return correction_as_a_search(data, integrity_information, address);
}



// called from setup_arch
void csi_rowhammer_init(void) {
	printk(KERN_INFO "handle_memory_corruption address %llx\n", virt_to_phys(handle_memory_corruption));
	printk(KERN_INFO "csi_mac address %llx\n", virt_to_phys(csi_mac));
	printk(KERN_INFO "correction_as_a_search address %llx\n", virt_to_phys(correction_as_a_search));
	printk(KERN_INFO "advanced_correction address %llx\n", virt_to_phys(advanced_correction));

	// set these pages "unflippable" to simulate the secure SRAM
	// the nops in advanced_correction are are flippable
	csi_stii(virt_to_phys(exc_csi_correction) & (~0xFFFUL), 1);
	csi_stii(virt_to_phys(correction_as_a_search) & (~0xFFFUL), 1);
	csi_stii(virt_to_phys(handle_memory_corruption) & (~0xFFFUL), 1);
}