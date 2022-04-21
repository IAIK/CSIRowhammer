#ifndef __CSIMEMCTRL_HH__
#define __CSIMEMCTRL_HH__

#include "arch/x86/interrupts.hh"
#include "arch/x86/x86_traits.hh"
#include "mem/mem_ctrl.hh"
#include "mem/mem_interface.hh"
#include "mem/packet.hh"
#include "mem/request.hh"
#include "params/CSIMemCtrl.hh"

class CSIMemCtrl : public MemCtrl
{
public:
    CSIMemCtrl(const CSIMemCtrlParams &params);

    //We need this class to hide the memory port of the regular memory controller class.
    class CSIMemoryPort : public MemCtrl::MemoryPort
    {

    public:
        CSIMemoryPort(const std::string &name, MemCtrl &_ctrl);

    protected:
        AddrRangeList getAddrRanges() const;
    };

    Port &getPort(const std::string &if_name,
                  PortID idx = InvalidPortID) override;

    class InterruptPort : public RequestPort
    {
        CSIMemCtrl &ctrl;

    public:
        InterruptPort(const std::string &name, CSIMemCtrl &_ctrl)
            : RequestPort(name, &_ctrl), ctrl(_ctrl)
        {}

        bool recvTimingReq(PacketPtr pkt)
        {
            return true;
        }

        void sendRetryResp() {}
       
        bool recvTimingResp(PacketPtr pkt)
        {
            return true;
        }

        void recvReqRetry() {}
    };

    void init();

    CSIMemoryPort port;

    size_t timestamp_mac_mismatch;
    size_t timestamp_mac_corrected;

protected:
    Tick recvAtomic(PacketPtr pkt) override;
    void accessAndRespond(PacketPtr pkt, Tick static_latency) override;
    void recvFunctional(PacketPtr pkt) override;

private:
    // Enum that describes the state of a memory access.
    enum AccessState
    {
        SUCCESS,
        MAC_MISMATCH,
    };

    // Enum that describes different access types. The access types
    // are determined by masking the address with the specific range mask
    enum AccessType
    {
        REGULAR,
        RAW_DATA,
        RAW_IIF
    };

    // Enum used to decide which call mode is being used
    enum CallMode
    {
        ATOMIC,
        FUNCTIONAL,
        TIMING
    };

    // Amount of bits used of the MAC (56)
    int mac_bits;
    // Size of memory blocks which are grouped and hashed (64 bytes)
    unsigned int block_size;
    // Latency for a single MAC calculation. Is added directly to the DRAM latency
    Tick mac_tick_latency;
    
    // Amount of memory accesses after which the next access will have a bit flipped
    std::vector<size_t> fail_after;
    // Physical address where a bit is flipped after a prior fail_after flip
    std::vector<size_t> fail_address;

    // The intergrity information is stored in a separate array simulating the ECC chips
    uint64_t *integrity_information_memory;
    // Size of the integrity information memory region
    size_t integrity_information_memory_size;

    // Contains the physical addresses of the pages where no bitflips can happen
    std::vector<Addr> secure_pages;

    // Calculate the start address of the block in which the current address is found
    Addr getBlockAddr(Addr addr);
    // Get the location of the integrity information for the addr.
    size_t getIntegrityInformationMemoryIndex(Addr addr);
    // Calculate the MAC of the data given by the data pointer
    uint64_t computeMAC(uint8_t *data, uint64_t physical_address);

    // Access memory and perform integrity check. Return MAC_MISMATCH if tags do not match
    AccessState performCSIAccess(PacketPtr pkt, bool isAtomic = false);
    void accessRawData(PacketPtr pkt);
    void accessRawIntegrityInformation(PacketPtr pkt);

    // Get pointer to memory at position specified by addr
    uint8_t *getMemPtr(Addr addr);
    
    // Decide the type of access by masking the address specified in the packet with the corresponding masks.
    static AccessType decideAccessType(PacketPtr pkt);
    void performAccessWithMode(PacketPtr pkt, CallMode mode, Tick *t = nullptr, bool *b = nullptr);
};

#endif