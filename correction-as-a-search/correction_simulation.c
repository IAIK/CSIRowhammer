#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#define DATA_SIZE 256
#define DATA_SIZE_BYTE_LOG 5    // log2(DATA_SIZE / 8)
#define MAX_FLIPS 8
#define CONST_NUM_PARITY_BITS 8
#define PARITY_BLOCK_SIZE (DATA_SIZE / CONST_NUM_PARITY_BITS)

#define MONTE_CARLO_RUNS 10000


#if MAX_FLIPS > 8
#error MAX_FLIPS must be <= 8
#endif

#if CONST_NUM_PARITY_BITS != 8
#error CONST_NUM_PARITY_BITS must be = 8
#endif


#define FLIPBIT(data,i) ((data[(i) / (8 * sizeof(*data))] ^= (1 << ((i) % (8 * sizeof(*data))))))


double mac_computations_list[MAX_FLIPS + 2];
uint64_t mac_computations_list_count[MAX_FLIPS + 2];
uint64_t durations[MAX_FLIPS + 2];
uint64_t durations_count[MAX_FLIPS + 2];

int num_7_flips = 0;
int num_8_flips = 0;


uint64_t ns() {
  struct timespec tp;
  clock_gettime(CLOCK_MONOTONIC, &tp);
  return ((uint64_t) tp.tv_sec) * 1000000000ULL + tp.tv_nsec;
}


volatile inline int COMPARE_DATA(const void *x, const void *y) {
  return memcmp(x, y, DATA_SIZE / 8) == 0;
}


uint8_t compute_parity(uint8_t data[]) {
  uint8_t parity = 0;
  int di = 0;
  for (int i = 0; i < CONST_NUM_PARITY_BITS; i++) {
    int p = 0;
    for (int j = 0; j < DATA_SIZE / (8 * CONST_NUM_PARITY_BITS); j++, di++) {
      p ^= __builtin_parity(data[di]);
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
int64_t correct(uint32_t data[], uint32_t correct_data[], uint8_t parity) {
  // counts how often the mac was computed, used for the performance analysis
  uint64_t mac_computations = 0;

  unsigned int i, j, p;

  uint8_t data_parity, parity_wrong;
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


  #if CONST_NUM_PARITY_BITS != 8 || PARITY_BLOCK_SIZE != 32
  #error change CONST_NUM_PARITY_BITS or PARITY_BLOCK_SIZE for the faulty connection correction
  #endif

  uint32_t *data_parity_block[8];
  uint32_t flip_mask[8];

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

  data_parity = compute_parity((uint8_t*)data);
  
  parity_wrong = data_parity ^ parity;
  min_flips = __builtin_popcount(parity_wrong);

  // Get the possible bit flip locations from the parity bits
  for (int p = 0; p < 8; p++) {
    if ((parity_wrong >> p) & 1) {
      prob_flip_positions[num_wrong_parity_bits] = p * PARITY_BLOCK_SIZE;
      num_wrong_parity_bits++;
    }
  }
 
  if (min_flips < 2) {
    // maybe there is no flip
    mac_computations++;
    if (COMPARE_DATA(data, correct_data)) {
      return mac_computations;
    }

    // there is a double flip in at least one parity bit input
    if (min_flips == 0) {
      no_parity_flip_min_flips = 2;
      double_flips_addition = 2;
    }
  }


  // This is the faulty connection correction. It is performed
  // in hardware, but we do it here to check the correctness
  // of the algorithm.

  // Assumption: A faulty DIMM connection always produces 0.
  // We search the data for offsets where all bits within 
  // the 32/64 bit parity blocks are 0.
  // Then we use these to check.
  // The parity bit is correct, because it is transferred
  // over other lines.
  // This gives us the information in which blocks a bit
  // went from 1 to 0 and in which blocks it was already 0.
  uint32_t data32or = data[0];

  for (i = 1; i < CONST_NUM_PARITY_BITS; i++) {
    data32or |= data[i];
  }

  for (i = 0; i < PARITY_BLOCK_SIZE; i++) {
    if ((data32or & (1 << i)) == 0) {
      // all bits at offset i in every parity block are 0
      // this offset is a candidate for a faulty connection
      // use the parity bits to find the blocks where a 1
      // should be

      for(j = 0; j < num_wrong_parity_bits; j++) {
        FLIPBIT(data, prob_flip_positions[j] + i);
      }

      mac_computations++;
      if (COMPARE_DATA(data, correct_data)) {
        return mac_computations;
      }

      for(j = 0; j < num_wrong_parity_bits; j++) {
        FLIPBIT(data, prob_flip_positions[j] + i);
      }
    }
  }
  
  // Here starts the correction as a search
  no_parity_flip_min_flips = min_flips + double_flips_addition;

  while (1) {
    // sanity check of the code to find bugs
    if (double_flips_addition > MAX_FLIPS) {
      printf("min_flips + double_flips_addition %d > %d\n", min_flips + double_flips_addition, MAX_FLIPS);
      exit(1);
    }

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
      new_min_flips = __builtin_popcount(parity_wrong) + double_flips_addition;


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
                              mac_computations++;

                              if (COMPARE_DATA(data, correct_data)) {
                                return mac_computations;
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
}




void one_correction_run() {
  int i, j, p;
  uint8_t data[DATA_SIZE / 8];
  uint8_t correct_data[DATA_SIZE / 8];
  uint8_t parity = 0;
  int flip_positions[MAX_FLIPS];
  int64_t mac_computations;
  uint64_t duration = 0;
  int num_flips;
  int parity_flip = -1;
  
  // create random data
  for (i = 0; i < DATA_SIZE / 8; i++) {
    data[i] = rand();
    correct_data[i] = data[i];
  }
  
  parity = compute_parity(data);
  
  num_flips = rand() % (MAX_FLIPS + 2);

  if (num_flips == 7) {
    num_7_flips++;
    // correct only 200 7 flips, otherwise it takes too long
    if (num_7_flips >= 200) {
      num_flips = rand() % 7;
    }
  }

  if (num_flips == 8) {
    num_8_flips++;
    // correct only 10 8 flips, otherwise it takes too long
    if (num_8_flips >= 10) {
      num_flips = rand() % 7;
    }
  }

  if (num_flips == MAX_FLIPS + 1) {
    // simulate a broken connection at position p
    p = rand() % PARITY_BLOCK_SIZE;
    
    uint32_t *data32 = (uint32_t*) data;

    // set all bits at position p to 0
    for (i = 0; i < CONST_NUM_PARITY_BITS; i++) {
      data32[i] = data32[i] & ~(1 << p);
    }
  }
  else {
    i = 0;

    if (num_flips > 0 && rand() % 32 == 0) {
      // flip a parity bit
      parity_flip = rand() % 8;
      parity ^= (1 << parity_flip);
      i++;
    }

    // flip bits
    for (; i < num_flips; i++) {
      // don't flip a bit twice
      do {
        p = rand() % DATA_SIZE;
        for (j = 0; j < i; j++) {
          if (flip_positions[j] == p)
            continue;
        }
      } while(0);
      
      FLIPBIT(data, p);
    }
  }

  // correct the bit flip and measure the time
  uint64_t start = ns();

  mac_computations = correct((uint32_t*) data, (uint32_t*) correct_data, parity);
  
  duration = ns() - start;

  // save stats
  durations[num_flips] += duration;
  durations_count[num_flips]++;
  
  mac_computations_list[num_flips] += mac_computations;
  mac_computations_list_count[num_flips]++;
}


int main(int argc, char *argv[]) {
  int i;
  int runs = MONTE_CARLO_RUNS;
  
  (void) argc;
  (void) argv;
    
  // main run loop
  for (i = 0; i < runs; i++) {
    one_correction_run();

    // show progress
    if (i % 10 == 0) {
      printf("\r%d / %d  ", i, runs);
      fflush(stdout);
    }
  }
  
  // print statistics
  printf("\r%d / %d\n", runs, runs);
  printf("#BF,     MAC comp.,    Duration (ns), MAC comp. duration (ns)\n");

  for (i = 0; i < MAX_FLIPS + 2; i++) {
    if (mac_computations_list_count[i] > 0) {
      if (i == MAX_FLIPS + 1) {
        printf("BC, ");
      } else {
        printf(" %d, ", i);
      }

      double mac_computation_duraction = mac_computations_list[i] / mac_computations_list_count[i] / 1.5 * 5.13;
      if (i <= 1 || i == MAX_FLIPS + 1)
        mac_computation_duraction = -1;

      printf("%14.2f, %16ld, %14.0f\n", mac_computations_list[i] / mac_computations_list_count[i],
              durations[i] / durations_count[i], mac_computation_duraction);
    }
  }
  
  return 0;
}
