
#include "CSIMemCtrl.hh"
#include "CSIAddr.h"
#include "IntegrityInformation.hh"
#include "debug/CSIRowhammer.hh"
#include "arch/x86/isa.hh"
#include "sim/core.hh"


CSIMemCtrl::CSIMemCtrl(const CSIMemCtrlParams &p) : MemCtrl(p),
                                                    port(name() + ".port", *this),
                                                    mac_bits(56),
                                                    block_size(64), 
                                                    mac_tick_latency(p.mac_tick_latency)
{
    //Check if address space can be divided in block size, as no padding
    //will be applied
    if (dram->size() % block_size != 0)
    {
        assert(false && "DRAM size has to be multiple of block size");
    }

    // Split up the fail argument in counter values and physical addresses
    for(auto &f : p.fail) {
        if (f > 0) {
            fail_after.push_back(f);
        }
        else {
            fail_address.push_back(-f);
        }
    }
    // Sort the counter values
    std::sort(fail_after.begin(), fail_after.end());

    //Set port of parent class, this is needed to make sure that
    //calls do not reference an unset port
    MemCtrl::port = &this->port;

    //Calculate integrity information memory size in bits
    integrity_information_memory_size = (dram->size() / block_size) * 64;
    
    //Round up to next multiple of 64 as we use uint64_t as the type for the memory region
    if (integrity_information_memory_size % 64 != 0)
    {
        integrity_information_memory_size = integrity_information_memory_size + 64 - (integrity_information_memory_size % 64);
    }
    DPRINTF(CSIRowhammer, "Integrity information memory region is %llu Mbytes in size\n", integrity_information_memory_size / 8 / 1024 / 1024);

    //Zero-initialized integrity information memory region
    //We divide by 64 as we have 8 bytes per entry and the integrity information memory size is currently given in bit
    integrity_information_memory = new uint64_t[integrity_information_memory_size / 64]();
    uint8_t *data = dram->toHostAddr(dram->getAddrRange().start());
    DPRINTF(CSIRowhammer, "DRAM starts at range 0x%llx and phys 0x%llx up to 0x%llx", dram->getAddrRange().start(), (size_t)data, dram->getAddrRange().end());
    DPRINTF(CSIRowhammer, "Fail after is\n");
    for (auto &f : fail_after)
        DPRINTF(CSIRowhammer, "  %llu\n", f);
}

// Pass to parent constructor, we only want to overwrite the address range method
CSIMemCtrl::CSIMemoryPort::CSIMemoryPort(const std::string &name, MemCtrl &_ctrl)
    : MemCtrl::MemoryPort(name, _ctrl)
{
}

// This method overwrites the accessAndRespond method of the standard memory controller.
// The main difference here is that we add the MAC latency to the overall latency
void
CSIMemCtrl::accessAndRespond(PacketPtr pkt, Tick static_latency)
{
    //DPRINTF(CSIRowhammer, "Responding to Address %lld.. \n",pkt->getAddr());
    bool wasSpecialRequest = false;
    bool needsResponse = pkt->needsResponse();
    // do the actual memory access which also turns the packet into a
    // response
    if (dram && dram->getAddrRange().contains(pkt->getAddr())) {
        dram->access(pkt);
    } else if (nvm && nvm->getAddrRange().contains(pkt->getAddr())) {
        nvm->access(pkt);
    } else {
        auto accessType = decideAccessType(pkt);
        wasSpecialRequest = true;
        if(accessType == AccessType::RAW_DATA)
        {
            accessRawData(pkt);
            pkt->makeResponse();
        }
        else if(accessType == AccessType::RAW_IIF)
        {
            accessRawIntegrityInformation(pkt);
            pkt->makeResponse();
        }
        else
        {
            panic("unknown access type");
        }
    }    

    // turn packet around to go back to requestor if response expected
    if (needsResponse) {
        // access already turned the packet into a response
        assert(pkt->isResponse());
        // response_time consumes the static latency and is charged also
        // with headerDelay that takes into account the delay provided by
        // the xbar and also the payloadDelay that takes into account the
        // number of data beats.
        // Add the latency of a MAC calculation on top of the response time.
        Tick mac_latency = (pkt->getSize() / 64) * mac_tick_latency;
        if (mac_latency == 0) {
            mac_latency = mac_tick_latency;
        }

        Tick response_time = curTick() + static_latency + pkt->headerDelay +
                             pkt->payloadDelay + mac_latency;

        // Here we reset the timing of the packet before sending it out.
        pkt->headerDelay = pkt->payloadDelay = 0;

        // queue the packet in the response queue to be sent out after
        // the static latency has passed
        port.schedTimingResp(pkt, response_time);
    } else {
        // @todo the packet is going to be deleted, and the MemPacket
        // is still having a pointer to it
        pendingDelete.reset(pkt);
    }

    // Check if the access is legit.
    if(!wasSpecialRequest)
    {
        auto eccResult = this->performCSIAccess(pkt, CallMode::ATOMIC);
        if (eccResult == AccessState::MAC_MISMATCH)
        {
            // Set packet cmd to error
            DPRINTF(CSIRowhammer, "Returning error from AccessAndRespond\n");
            pkt->cmd = MemCmd::BadMACError;
            // Set timestamp when error was caught in the memory controllers
            this->timestamp_mac_mismatch = curTick();
            DPRINTF(CSIRowhammer, "Detected MAC mismatch at cycle %lu\n", this->timestamp_mac_mismatch);
            return;
        }
    }
}


// Return modified address ranges so that we can map the
// RAM multiple times in the address space
AddrRangeList CSIMemCtrl::CSIMemoryPort::getAddrRanges() const
{
    AddrRangeList ranges;
    //Get ranges from parent and modify them
    auto baseRanges = this->MemoryPort::getAddrRanges();
    for (auto range : baseRanges)
    {
        ranges.push_back(range);

        AddrRange get_data_range(ECC_GET_DATA_MASK | range.start(), ECC_GET_DATA_MASK | range.end(), range.get_masks(), range.get_intlvMatch());
        AddrRange get_iif_range(ECC_GET_IIF_MASK | range.start(), ECC_GET_IIF_MASK | range.end(), range.get_masks(), range.get_intlvMatch());
        ranges.push_back(get_data_range);
        ranges.push_back(get_iif_range);

        DPRINTF(CSIRowhammer, "    %s\n", get_data_range.to_string());
        DPRINTF(CSIRowhammer, "    %s\n", get_iif_range.to_string());
    }
    return ranges;
}

void CSIMemCtrl::init()
{
    if (!port.isConnected())
        fatal("CSIMemCtrl %s is unconnected!\n", name());
    else
        port.sendRangeChange();
}

Port &CSIMemCtrl::getPort(const std::string &if_name, PortID idx)
{
    if (if_name == "port")
        return this->port;
    else
        return MemCtrl::getPort(if_name, idx);
}

// Calculate the index in the integrity information memory array from a given physical address
size_t CSIMemCtrl::getIntegrityInformationMemoryIndex(Addr addr)
{
    //First we get the block in which the address is found
    auto block_addr = this->getBlockAddr(addr);
    //This is the integrity information sized block index, we need to calculate the
    //true index by considering the 64 bit sized array
    auto overall_block_index = block_addr / this->block_size;
    return overall_block_index;
}

// Perform access to packet-specified memory address while performing an
// integrity check
CSIMemCtrl::AccessState CSIMemCtrl::performCSIAccess(PacketPtr pkt, bool isAtomic)
{

    static size_t counter = 0;
    // Do not count atomic requests
    if(isAtomic == false)
    {
        counter++;

        if (counter % 200000 == 0) {
            DPRINTF(CSIRowhammer, "Counter Value: %ld %lx %ld\n", counter, pkt->getAddr() & (~0xFFFUL), std::count(secure_pages.begin(), secure_pages.end(), pkt->getAddr() & (~0xFFFUL)));
        }
    }
    auto block_base_addr = this->getBlockAddr(pkt->getAddr());
    volatile size_t overall_size = pkt->getSize();
    if (pkt->getAddr() - block_base_addr != 0)
    {
        overall_size += pkt->getAddr() - block_base_addr;
    }
    if (overall_size % this->block_size != 0)
    {
        overall_size = overall_size + this->block_size - (overall_size % this->block_size);
    }
    // Calculate the amount of blocks that are actually accessed. This is
    // needed if accesses are not block-aligned and cover more than one block
    size_t index_count = overall_size / this->block_size;
    uint8_t *data = this->getMemPtr(block_base_addr);
    auto base_index = this->getIntegrityInformationMemoryIndex(block_base_addr);

    if (pkt->isRead())
    {
        bool should_detect = false;
        bool detected = false;
        uint8_t data_correct[overall_size];
        memcpy(data_correct, data, overall_size);
        // Check MACs for all blocks that have been accessed
        for (size_t idx = 0; idx < index_count; idx++)
        {
            if (!isAtomic && // dont corrupt atomic reads that come e.g., from gdb
                !should_detect && // dont corrupt if a not detected corruption is pending (for debugging)
                fail_after.size() != 0 && *(fail_after.begin()) != 0 && counter != 0 && counter >= *(fail_after.begin()) && 
                this->integrity_information_memory[base_index + idx] != 0 && // dont corrupt memory that has no MAC (from kvm restore)
                std::count(secure_pages.begin(), secure_pages.end(), pkt->getAddr() & (~0xFFFUL)) == 0 ) // dont corrupt secure memory
            {
                volatile auto index = rand() % overall_size;
                auto off = rand() % 8;
                
                DPRINTF(CSIRowhammer, "Inducing bit flip at %lx, index is %lu and offset is %lu\n", block_base_addr, index, off);
                DPRINTF(CSIRowhammer, "Data before: %d\n", data[index]);

                data[index] ^= (1 << off);

                DPRINTF(CSIRowhammer, "Data after: %d\n", data[index]);
                should_detect = true;

                // remove the first element
                fail_after.erase(fail_after.begin());

                // flip a bit from flip_address if available
                if (fail_address.size() > 0) {
                    auto block_base_addr_ = this->getBlockAddr(fail_address[0]);
                    auto data_ = this->getMemPtr(block_base_addr_);
                    auto index_ = rand() % block_size;
                    auto off_ = rand() % 8;
                    
                    DPRINTF(CSIRowhammer, "Inducing bit flip at fail_address %lx offset is %lu\n", block_base_addr_, index_, off_);
                    DPRINTF(CSIRowhammer, "Data before: %d\n", data_[index_]);
                    data_[index_] ^= (1 << off_);
                    DPRINTF(CSIRowhammer, "Data after: %d\n", data_[index_]);

                    // remove the first element
                    fail_address.erase(fail_address.begin());
                }
            }
            // Calculate MAC of accessed memory, compare to stored MAC
            auto mac = this->computeMAC(data + this->block_size * idx, block_base_addr);
            if (mac != (this->integrity_information_memory[base_index + idx] & ((1ULL << mac_bits) - 1)))
            {
                // During kvm execution, memory content is overwritten without using
                // the memory controller, we cannot catch these changes
                if (this->integrity_information_memory[base_index + idx] == 0)
                {
                    uint64_t integrity_information = mac;
                    integrity_information |= ((uint64_t) IntegrityInformation::calculateParityBits(data + this->block_size * idx));
                    this->integrity_information_memory[base_index + idx] = integrity_information;
                    continue;
                }
                DPRINTF(CSIRowhammer, "Checksum mismatch at read of size %lu at address 0x%llx - %llx != %llx\n", pkt->getSize(), (uint64_t)pkt->getAddr(), mac, this->integrity_information_memory[base_index + idx]);
                DPRINTF(CSIRowhammer, "Block base address is 0x%llx\n", (size_t)block_base_addr);
                detected = true;
                // If we have detected a fault we can immediately break and continue to
                // the part where we signal the CPU that something is wrong
                break;
            }
        }
        if (detected)
        {
            // This is just debug output.
            for (size_t i = 0; i < index_count; i++)
            {
                auto mac = this->computeMAC(data + this->block_size * i, block_base_addr);
                DPRINTF(CSIRowhammer, "MACs: 0x%llx vs 0x%llx\n", mac, this->integrity_information_memory[base_index + i]);
            }
            for (int i = 0; i < overall_size / 8; i++)
            {
                DPRINTF(CSIRowhammer, "Value: %llx vs %llx\n", ((uint64_t *)data)[i], ((uint64_t *)data_correct)[i]);
            }
            return AccessState::MAC_MISMATCH;
        }
        if(should_detect && !detected)
        {
            std::cout << "Should have detected that!\n";
            std::cout << "packet size is " << pkt->getSize() << std::endl;
            std::cout << "Data is ";
            for (int i = 0; i < overall_size / 8; i++)
            {
                DPRINTF(CSIRowhammer, "Value: %llx vs %llx\n", ((uint64_t *)data)[i], ((uint64_t *)data_correct)[i]);
            }
            panic("should have detected that mismatch\n");
        }
    }

    // We need to update the integrity information. If the packet is not aligned we need to update the whole block
    else if (pkt->isWrite())
    {
        auto offset = pkt->getAddr() - block_base_addr;
        uint8_t tmp[overall_size] = {0};
        memcpy(tmp, data, overall_size);
        memcpy(tmp + offset, pkt->getPtr<uint8_t>(), pkt->getSize());
        for (size_t idx = 0; idx < index_count; idx++)
        {
            auto mac = this->computeMAC(tmp + this->block_size * idx, block_base_addr);
            uint64_t integrity_information = mac;
            integrity_information |= ((uint64_t) IntegrityInformation::calculateParityBits(tmp + this->block_size * idx) << 56);

            this->integrity_information_memory[base_index + idx] = integrity_information;
        }
    }
    return AccessState::SUCCESS;
}

// Get start address of memory block that contains addr
Addr CSIMemCtrl::getBlockAddr(Addr addr)
{
    //Round down to multiple of block size
    return (addr / block_size) * block_size;
}

CSIMemCtrl::AccessType CSIMemCtrl::decideAccessType(PacketPtr pkt)
{
    if ((pkt->getAddr() & ECC_GET_DATA_MASK) == ECC_GET_DATA_MASK)
    {
        return CSIMemCtrl::AccessType::RAW_DATA;
    }
    if ((pkt->getAddr() & ECC_GET_IIF_MASK) == ECC_GET_IIF_MASK)
    {
        return CSIMemCtrl::AccessType::RAW_IIF;
    }
    return CSIMemCtrl::AccessType::REGULAR;
}

// Get a pointer to the actual memory storing the data of the simulated system
uint8_t *CSIMemCtrl::getMemPtr(Addr addr)
{
    return dram->toHostAddr(addr);
}

Tick CSIMemCtrl::recvAtomic(PacketPtr pkt)
{
    Tick result;
    performAccessWithMode(pkt, CallMode::ATOMIC, &result);

    return 0;
}



void CSIMemCtrl::recvFunctional(PacketPtr pkt)
{
    performAccessWithMode(pkt, CallMode::FUNCTIONAL);
}


// Calculate the MAC
uint64_t CSIMemCtrl::computeMAC(uint8_t *data, uint64_t physical_address)
{
    return IntegrityInformation::computeMAC(data, physical_address, mac_bits);
}


// Access data without triggering an error, this effectively bypasses all integrity checks.
void CSIMemCtrl::accessRawData(PacketPtr pkt)
{
    auto actual_addr = pkt->getAddr() ^ ECC_GET_DATA_MASK;
    actual_addr = this->getBlockAddr(actual_addr);
    DPRINTF(CSIRowhammer, "Access to masked address %llx with size %llx\n", actual_addr, pkt->getSize());
    if (pkt->isRead())
    {
        DPRINTF(CSIRowhammer, "Memory location content is\n");
        auto p = (uint64_t *)this->getMemPtr(actual_addr);
        for (int i = 0; i < pkt->getSize() / 8; i++)
        {
            DPRINTF(CSIRowhammer, "%llx\n", p[i]);
        }
        memcpy(pkt->getPtr<uint8_t>(), this->getMemPtr(actual_addr), pkt->getSize());
        DPRINTF(CSIRowhammer, "copied content\n");
    }
    else if (pkt->isWrite())
    {
        DPRINTF(CSIRowhammer, "Access is write!\n");
        memcpy(this->getMemPtr(actual_addr), pkt->getPtr<uint8_t>(), pkt->getSize());
        auto block_base_addr = this->getBlockAddr(actual_addr);
        auto base_index = this->getIntegrityInformationMemoryIndex(block_base_addr);
        auto mac = computeMAC(this->getMemPtr(block_base_addr), actual_addr);
        if (mac == (this->integrity_information_memory[base_index] & ((1ULL << mac_bits) - 1)))
        {
            DPRINTF(CSIRowhammer, "Correction found!\n");
        }
    }
}
// Access integrity information corresponding to a certain address specified in pkt
void CSIMemCtrl::accessRawIntegrityInformation(PacketPtr pkt)
{
    auto actual_addr = pkt->getAddr() ^ ECC_GET_IIF_MASK;
    DPRINTF(CSIRowhammer, "Access to integrity information address %llx with size %llx\n", actual_addr, pkt->getSize());
    auto block_base_addr = this->getBlockAddr(actual_addr);
    auto base_index = this->getIntegrityInformationMemoryIndex(block_base_addr);

    if (pkt->isRead())
    {
        auto integrity_information = this->integrity_information_memory[base_index];
        DPRINTF(CSIRowhammer, "The integrity_information is %llx\n", integrity_information);
        memset(pkt->getPtr<uint8_t>(), 0, pkt->getSize());
        *(pkt->getPtr<uint64_t>()) = integrity_information;
    }
    else if (pkt->isWrite())
    {
        // Writing the integrity information is not possible.
        // We use this to inform the memory controller of the
        // physical addresses where no bitflip can happen to
        // simulate the "secure SRAM" as we have not really
        // implemented it in gem5
        if (*(pkt->getPtr<uint64_t>())) {
            secure_pages.push_back(actual_addr & (~0xFFFUL));
        }
    }
}

// Perform access according to pkt using a specified mode. Returns ticks and true/false
// according to selected mode and result of ECC check
void CSIMemCtrl::performAccessWithMode(PacketPtr pkt, CallMode mode, Tick *t, bool *b)
{
    auto accessType = decideAccessType(pkt);
    switch (accessType)
    {
    case AccessType::RAW_DATA:
    {
        accessRawData(pkt);
        if (t)
            *t = 0;
        if (b)
            *b = true;
        if (pkt->needsResponse())
        {
            pkt->makeResponse();
        }
        return;
    }
    case AccessType::RAW_IIF:
    {
        accessRawIntegrityInformation(pkt);
        if (t)
            *t = 0;
        if (b)
            *b = true;
        if (pkt->needsResponse())
        {
            pkt->makeResponse();
        }
        return;
    }
    default:
        break;
    }

    auto access_result = this->performCSIAccess(pkt, mode == CallMode::ATOMIC || mode == CallMode::FUNCTIONAL);
    
    if (access_result == AccessState::MAC_MISMATCH)
    {
        // Set packet cmd to error
        if (pkt->needsResponse())
        {
            pkt->makeResponse();
        }
        pkt->cmd = MemCmd::BadAddressError;
        return;
    }

    switch (mode)
    {
    case CallMode::ATOMIC:
    {
        auto result = MemCtrl::recvAtomic(pkt);
        *t = result;
        break;
    }
    case CallMode::FUNCTIONAL:
    {
        MemCtrl::recvFunctional(pkt);
        break;
    }
    case CallMode::TIMING:
    {
        auto result = MemCtrl::recvTimingReq(pkt);
        *b = result;
        break;
    }
    }

    if (access_result == AccessState::MAC_MISMATCH)
    {
        // Set packet cmd to error
        pkt->cmd = MemCmd::BadMACError;
    }
}
