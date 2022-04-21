#include "IntegrityInformation.hh"
#include <cstring>
#include <iostream>
#include "qarma64tc_7.h"

namespace IntegrityInformation
{
    uint8_t *key = NULL;

    void set_block_number_in_tweak(u8 * tweak, const u64 block_no)
    {
        tweak[1] = (block_no >> 56ULL) & 0xf;
        tweak[2] = (block_no >> 52ULL) & 0xf;
        tweak[3] = (block_no >> 48ULL) & 0xf;
        tweak[4] = (block_no >> 44ULL) & 0xf;
        tweak[5] = (block_no >> 40ULL) & 0xf;
        tweak[6] = (block_no >> 36ULL) & 0xf;
        tweak[7] = (block_no >> 32ULL) & 0xf;
        tweak[8] = (block_no >> 28ULL) & 0xf;
        tweak[9] = (block_no >> 24ULL) & 0xf;
        tweak[10] = (block_no >> 20ULL) & 0xf;
        tweak[11] = (block_no >> 16ULL) & 0xf;
        tweak[12] = (block_no >> 12ULL) & 0xf;
        tweak[13] = (block_no >> 8ULL) & 0xf;
        tweak[14] = (block_no >> 4ULL) & 0xf;
        tweak[15] = (block_no >> 0ULL) & 0xf;
    }

    //CSI:Rowhammer: Compute MAC with QARMA
    uint64_t computeMAC(uint8_t *data, uint64_t physical_address, int checksum_bits)
    {
        volatile uint64_t final_result = 0;
        unsigned char input_data[16] = {0}, tweak[16] = {0}, tweakl[32], output[16], result[16] = {0};
        unsigned char k0[16], w0[16], k1[16], k1_M[16], w1[16], whitekey[16];
        
        // if (key == nullptr)
        // {
        //     //std::cout << "initializing key for MAC computation\n";
        //     srand(time(NULL));
        //     for (int i = 0; i < 32; i++)
        //     {
        //         key[i] = rand() & 0xf;
        //     }
        // }

        // We use this key for testing purposes. When using a random key, we cannot restore from a checkpoint
        if (key == nullptr)
        {
            key = (uint8_t*) malloc(32);
            for (int i = 0; i < 32; i++)
            {
                key[i] = i & 0xf;
            }
        }

        for (int i = 0; i < 16; i++) {
            w0[i] = key[i];
            k0[i] = key[16 + i];
        }

        KeySpecialisation(k0, w0, k1, k1_M, w1);

        //We need to split the data into 8 blocks
        for (int share = 0; share < 8; share++)
        {
            memset(whitekey, share, 16);
            memset(tweakl, 0, 32);

            // split input data into single bytes
            for (int j = 0, k = 0; j < 16; k++) {
                input_data[j++] = (data + share * 8)[k] & 0xf;
                input_data[j++] = (data + share * 8)[k] >> 4;
            }
            
            // the physical address is the tweak
            set_block_number_in_tweak(tweakl, (physical_address >> 6) + share);

            if (share == 7) {
                for (int j = 0; j < 16; j++) {
                    input_data[j] ^= result[j];
                }
            }

            tweakcompression(tweakl, k0, k1, tweak, whitekey);
            
            qarma64(input_data, w0, w1, k0, k1, tweak, whitekey, 7, 1, output);

            if (share < 7) {
                for (int j = 0; j < 16; j++) {
                    result[j] ^= output[j];
                }
            }
            else {
                final_result = 0;
                for (int j = 0; j < 16; j++) {
                    final_result |= ((uint64_t) (output[j])) << (j * 4);
                }
            }
        }

        return final_result & ((1ULL << checksum_bits) - 1);
    }

    uint8_t calculateParityBits(uint8_t *data)
    {
        uint8_t parity = 0;
        int i, j, p, di = 0;
        for (i = 0; i < 8; i++) {
        p = 0;
        for (j = 0; j < 512 / 64; j++, di++) {
            p ^= __builtin_parity(data[di]);
        }
        parity |= (p << i);
        }
        return parity;
    }
}