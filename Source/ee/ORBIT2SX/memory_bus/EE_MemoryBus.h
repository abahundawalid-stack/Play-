#pragma once

#include "../memory/EE_Memory.h"
#include "../memory_map/EE_MemoryMap.h"

#include <cstddef>
#include <cstdint>

namespace ORBIT2SX
{
namespace EE
{
namespace Bus
{

enum class DeviceStatus : std::uint8_t
{
    Ok = 0,
    UnsupportedAccess,
    UnmappedRegister,
    AccessFault,
    Busy,
    InternalError
};

enum class BusStatus : std::uint8_t
{
    Ok = 0,
    InvalidArgument,
    InvalidAccessWidth,
    MapFailure,
    BackingStoreFailure,
    DeviceNotAttached,
    DeviceFailure
};

struct BusResult
{
    BusStatus status;
    MemoryMap::Status mapStatus;
    Memory::Status memoryStatus;
    DeviceStatus deviceStatus;
};

// Device implementations own their register/local-memory state.
// One endpoint may be attached to multiple regions (e.g. VU0 micro + data).
// The caller owns endpoint lifetime and must detach before destroying it.
class IEEBusDevice
{
public:
    virtual ~IEEBusDevice() {}

    virtual DeviceStatus Read(
        MemoryMap::RegionId region,
        std::uint32_t regionOffset,
        MemoryMap::AccessType access,
        void* destination,
        std::size_t length) = 0;

    virtual DeviceStatus Write(
        MemoryMap::RegionId region,
        std::uint32_t regionOffset,
        const void* source,
        std::size_t length) = 0;
};

// EE_MemoryBus dispatches already-decoded addresses to backing storage or
// device-owned endpoints. It does not perform TLB/COP0 translation,
// CPU alignment exceptions, timing, DMA arbitration, or cache coherency.
// Calls are not internally synchronized; serialize access externally.
class EE_MemoryBus final
{
public:
    explicit EE_MemoryBus(Memory::EE_Memory& memory);

    EE_MemoryBus(const EE_MemoryBus&) = delete;
    EE_MemoryBus& operator=(const EE_MemoryBus&) = delete;

    BusResult AttachDevice(
        MemoryMap::RegionId region,
        IEEBusDevice* device);

    BusResult DetachDevice(MemoryMap::RegionId region);
    void DetachAllDevices();

    BusResult ReadVirtual(
        std::uint32_t virtualAddress,
        void* destination,
        std::size_t length);

    BusResult WriteVirtual(
        std::uint32_t virtualAddress,
        const void* source,
        std::size_t length);

    BusResult FetchVirtual(
        std::uint32_t virtualAddress,
        void* destination,
        std::size_t length);

    BusResult ReadPhysical(
        std::uint32_t physicalAddress,
        void* destination,
        std::size_t length);

    BusResult WritePhysical(
        std::uint32_t physicalAddress,
        const void* source,
        std::size_t length);

    // Entry point for a future CPU transaction layer after translation.
    BusResult ReadResolved(
        const MemoryMap::ResolvedAddress& resolved,
        MemoryMap::AccessType access,
        void* destination,
        std::size_t length);

    BusResult WriteResolved(
        const MemoryMap::ResolvedAddress& resolved,
        const void* source,
        std::size_t length);

    bool IsDeviceAttached(MemoryMap::RegionId region) const;

private:
    static const std::size_t kRegionSlotCount =
        static_cast<std::size_t>(MemoryMap::RegionId::Scratchpad) + 1;

    static BusResult MakeResult(BusStatus status);

    static bool IsValidRegionId(
        MemoryMap::RegionId region);

    static bool IsValidAccessType(
        MemoryMap::AccessType access);

    static bool IsRangeValid(
        std::uint32_t regionSize,
        std::uint32_t offset,
        std::size_t length);

    static Memory::Region ToMemoryRegion(
        MemoryMap::BackingRegion region,
        bool& valid);

    BusResult ValidateResolved(
        const MemoryMap::ResolvedAddress& resolved,
        MemoryMap::AccessType access,
        std::size_t length) const;

    BusResult DispatchRead(
        const MemoryMap::ResolvedAddress& resolved,
        MemoryMap::AccessType access,
        void* destination,
        std::size_t length);

    BusResult DispatchWrite(
        const MemoryMap::ResolvedAddress& resolved,
        const void* source,
        std::size_t length);

    Memory::EE_Memory& m_memory;

    IEEBusDevice* m_devices[kRegionSlotCount];
};

} // namespace Bus
} // namespace EE
} // namespace ORBIT2SX
