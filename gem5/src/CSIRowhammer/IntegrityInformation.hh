#ifndef INTEGRITYINFORMATION_GUARD
#define INTEGRITYINFORMATION_GUARD

#include <cstdint>

namespace IntegrityInformation
{
    uint64_t computeMAC(uint8_t *data, uint64_t physical_address, int checksum_bits);
    uint8_t calculateParityBits(uint8_t *data);
}

#endif