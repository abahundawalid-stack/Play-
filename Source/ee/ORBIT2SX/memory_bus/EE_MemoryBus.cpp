#include "EE_MemoryBus.h"

namespace ORBIT2SX
{
namespace EE
{
namespace Bus
{

namespace
{
bool HasPermission(
    std::uint8_t permissions,
    MemoryMap::AccessType access)
{
    return (permissions &
        static_cast<std::uint8_t>(access)) != 0;
}
}

EE_MemoryBus::EE_MemoryBus(Memory::EE_Memory& memory)
    : m_memory(memory)
{
    for(std::size_t i = 0; i < kRegionSlotCount; ++i)
    {
        m_devices[i] = nullptr;
    }
}

BusResult EE_MemoryBus::MakeResult(BusStatus status)
{
    BusResult result;

    result.status = status;
    result.mapStatus = MemoryMap::Status::Ok;
    result.memoryStatus = Memory::Status::Ok;
    result.deviceStatus = DeviceStatus::Ok;

    return result;
}

bool EE_MemoryBus::IsValidRegionId(
    MemoryMap::RegionId region)
{
    return static_cast<std::size_t>(region) <
           kRegionSlotCount;
}

bool EE_MemoryBus::IsValidAccessType(
    MemoryMap::AccessType access)
{
    return access == MemoryMap::AccessType::Read ||
           access == MemoryMap::AccessType::Write ||
           access == MemoryMap::AccessType::Execute;
}

bool EE_MemoryBus::IsRangeValid(
    std::uint32_t regionSize,
    std::uint32_t offset,
    std::size_t length)
{
    if(offset > regionSize) return false;

    return length <=
        static_cast<std::size_t>(regionSize - offset);
}

Memory::Region EE_MemoryBus::ToMemoryRegion(
    MemoryMap::BackingRegion region,
    bool& valid)
{
    valid = true;

    switch(region)
    {
    case MemoryMap::BackingRegion::MainRam:
        return Memory::Region::MainRam;

    case MemoryMap::BackingRegion::Scratchpad:
        return Memory::Region::Scratchpad;

    case MemoryMap::BackingRegion::BiosRom:
        return Memory::Region::BiosRom;

    case MemoryMap::BackingRegion::None:
    default:
        valid = false;
        return Memory::Region::MainRam;
    }
}

// ------------------------------------------------------------
// DEVICE REGISTRATION
// ------------------------------------------------------------

BusResult EE_MemoryBus::AttachDevice(
    MemoryMap::RegionId region,
    IEEBusDevice* device)
{
    if(device == nullptr || !IsValidRegionId(region))
    {
        return MakeResult(BusStatus::InvalidArgument);
    }

    const MemoryMap::RegionDescriptor* descriptor =
        MemoryMap::EE_MemoryMap::FindRegion(region);

    if(descriptor == nullptr ||
       descriptor->target != MemoryMap::MappingTarget::DeviceOwned)
    {
        return MakeResult(BusStatus::InvalidArgument);
    }

    m_devices[static_cast<std::size_t>(region)] = device;

    return MakeResult(BusStatus::Ok);
}

BusResult EE_MemoryBus::DetachDevice(
    MemoryMap::RegionId region)
{
    if(!IsValidRegionId(region))
    {
        return MakeResult(BusStatus::InvalidArgument);
    }

    const MemoryMap::RegionDescriptor* descriptor =
        MemoryMap::EE_MemoryMap::FindRegion(region);

    if(descriptor == nullptr ||
       descriptor->target != MemoryMap::MappingTarget::DeviceOwned)
    {
        return MakeResult(BusStatus::InvalidArgument);
    }

    m_devices[static_cast<std::size_t>(region)] = nullptr;

    return MakeResult(BusStatus::Ok);
}

void EE_MemoryBus::DetachAllDevices()
{
    for(std::size_t i = 0; i < kRegionSlotCount; ++i)
    {
        m_devices[i] = nullptr;
    }
}

// ------------------------------------------------------------
// RESOLVED ADDRESS VALIDATION
// ------------------------------------------------------------

BusResult EE_MemoryBus::ValidateResolved(
    const MemoryMap::ResolvedAddress& resolved,
    MemoryMap::AccessType access,
    std::size_t length) const
{
    if(!IsValidRegionId(resolved.region) ||
       !IsValidAccessType(access))
    {
        return MakeResult(BusStatus::InvalidArgument);
    }

    if(!MemoryMap::EE_MemoryMap::IsValidAccessWidth(length))
    {
        return MakeResult(BusStatus::InvalidAccessWidth);
    }

    const MemoryMap::RegionDescriptor* descriptor =
        MemoryMap::EE_MemoryMap::FindRegion(resolved.region);

    if(descriptor == nullptr)
    {
        return MakeResult(BusStatus::MapFailure);
    }

    if(descriptor->target != resolved.target ||
       descriptor->backingRegion != resolved.backingRegion)
    {
        return MakeResult(BusStatus::MapFailure);
    }

    if(!HasPermission(descriptor->permissions, access) ||
       !HasPermission(resolved.permissions, access))
    {
        BusResult result = MakeResult(BusStatus::MapFailure);

        result.mapStatus =
            MemoryMap::Status::PermissionDenied;

        return result;
    }

    if(!IsRangeValid(
        descriptor->sizeBytes,
        resolved.regionOffset,
        length))
    {
        BusResult result = MakeResult(BusStatus::MapFailure);

        result.mapStatus =
            MemoryMap::Status::AccessCrossesRegion;

        return result;
    }

    // Scratchpad is a virtual-only mapping in this map.
    if(resolved.region == MemoryMap::RegionId::Scratchpad)
    {
        if(resolved.hasPhysicalAddress ||
           resolved.physicalAddress != 0)
        {
            return MakeResult(BusStatus::MapFailure);
        }
    }
    else
    {
        if(!resolved.hasPhysicalAddress)
        {
            return MakeResult(BusStatus::MapFailure);
        }

        // Do not trust caller-created ResolvedAddress objects.
        // Revalidate physical address, region and offset.
        MemoryMap::ResolvedAddress verified;

        const MemoryMap::Status mapStatus =
            MemoryMap::EE_MemoryMap::ResolvePhysical(
                resolved.physicalAddress,
                length,
                access,
                verified);

        if(mapStatus != MemoryMap::Status::Ok ||
           verified.region != resolved.region ||
           verified.regionOffset != resolved.regionOffset)
        {
            BusResult result = MakeResult(BusStatus::MapFailure);

            result.mapStatus =
                mapStatus == MemoryMap::Status::Ok
                ? MemoryMap::Status::Unmapped
                : mapStatus;

            return result;
        }
    }

    return MakeResult(BusStatus::Ok);
}

// ------------------------------------------------------------
// READ DISPATCH
// ------------------------------------------------------------

BusResult EE_MemoryBus::DispatchRead(
    const MemoryMap::ResolvedAddress& resolved,
    MemoryMap::AccessType access,
    void* destination,
    std::size_t length)
{
    if(length != 0 && destination == nullptr)
    {
        return MakeResult(BusStatus::InvalidArgument);
    }

    const BusResult validation =
        ValidateResolved(resolved, access, length);

    if(validation.status != BusStatus::Ok)
    {
        return validation;
    }

    // Main RAM, Scratchpad and BIOS.
    if(resolved.target ==
       MemoryMap::MappingTarget::EeBackingStore)
    {
        bool validRegion = false;

        const Memory::Region memoryRegion =
            ToMemoryRegion(
                resolved.backingRegion,
                validRegion);

        if(!validRegion)
        {
            return MakeResult(BusStatus::MapFailure);
        }

        BusResult result = MakeResult(BusStatus::Ok);

        result.memoryStatus = m_memory.ReadBytes(
            memoryRegion,
            static_cast<std::size_t>(resolved.regionOffset),
            destination,
            length);

        if(result.memoryStatus != Memory::Status::Ok)
        {
            result.status = BusStatus::BackingStoreFailure;
        }

        return result;
    }

    // Device-owned region.
    const std::size_t slot =
        static_cast<std::size_t>(resolved.region);

    IEEBusDevice* device = m_devices[slot];

    if(device == nullptr)
    {
        return MakeResult(BusStatus::DeviceNotAttached);
    }

    BusResult result = MakeResult(BusStatus::Ok);

    result.deviceStatus = device->Read(
        resolved.region,
        resolved.regionOffset,
        access,
        destination,
        length);

    if(result.deviceStatus != DeviceStatus::Ok)
    {
        result.status = BusStatus::DeviceFailure;
    }

    return result;
}

// ------------------------------------------------------------
// WRITE DISPATCH
// ------------------------------------------------------------

BusResult EE_MemoryBus::DispatchWrite(
    const MemoryMap::ResolvedAddress& resolved,
    const void* source,
    std::size_t length)
{
    if(length != 0 && source == nullptr)
    {
        return MakeResult(BusStatus::InvalidArgument);
    }

    const BusResult validation = ValidateResolved(
        resolved,
        MemoryMap::AccessType::Write,
        length);

    if(validation.status != BusStatus::Ok)
    {
        return validation;
    }

    if(resolved.target ==
       MemoryMap::MappingTarget::EeBackingStore)
    {
        bool validRegion = false;

        const Memory::Region memoryRegion =
            ToMemoryRegion(
                resolved.backingRegion,
                validRegion);

        if(!validRegion)
        {
            return MakeResult(BusStatus::MapFailure);
        }

        BusResult result = MakeResult(BusStatus::Ok);

        result.memoryStatus = m_memory.WriteBytes(
            memoryRegion,
            static_cast<std::size_t>(resolved.regionOffset),
            source,
            length);

        if(result.memoryStatus != Memory::Status::Ok)
        {
            result.status = BusStatus::BackingStoreFailure;
        }

        return result;
    }

    const std::size_t slot =
        static_cast<std::size_t>(resolved.region);

    IEEBusDevice* device = m_devices[slot];

    if(device == nullptr)
    {
        return MakeResult(BusStatus::DeviceNotAttached);
    }

    BusResult result = MakeResult(BusStatus::Ok);

    result.deviceStatus = device->Write(
        resolved.region,
        resolved.regionOffset,
        source,
        length);

    if(result.deviceStatus != DeviceStatus::Ok)
    {
        result.status = BusStatus::DeviceFailure;
    }

    return result;
}

// ------------------------------------------------------------
// ALREADY-RESOLVED ACCESS
// ------------------------------------------------------------

BusResult EE_MemoryBus::ReadResolved(
    const MemoryMap::ResolvedAddress& resolved,
    MemoryMap::AccessType access,
    void* destination,
    std::size_t length)
{
    if(!MemoryMap::EE_MemoryMap::IsValidAccessWidth(length))
    {
        return MakeResult(BusStatus::InvalidAccessWidth);
    }

    if(access != MemoryMap::AccessType::Read &&
       access != MemoryMap::AccessType::Execute)
    {
        return MakeResult(BusStatus::InvalidArgument);
    }

    return DispatchRead(
        resolved,
        access,
        destination,
        length);
}

BusResult EE_MemoryBus::WriteResolved(
    const MemoryMap::ResolvedAddress& resolved,
    const void* source,
    std::size_t length)
{
    if(!MemoryMap::EE_MemoryMap::IsValidAccessWidth(length))
    {
        return MakeResult(BusStatus::InvalidAccessWidth);
    }

    return DispatchWrite(
        resolved,
        source,
        length);
}

// ------------------------------------------------------------
// VIRTUAL ACCESS
// ------------------------------------------------------------

BusResult EE_MemoryBus::ReadVirtual(
    std::uint32_t address,
    void* destination,
    std::size_t length)
{
    if(!MemoryMap::EE_MemoryMap::IsValidAccessWidth(length))
    {
        return MakeResult(BusStatus::InvalidAccessWidth);
    }

    if(length != 0 && destination == nullptr)
    {
        return MakeResult(BusStatus::InvalidArgument);
    }

    MemoryMap::ResolvedAddress resolved;

    const MemoryMap::Status status =
        MemoryMap::EE_MemoryMap::ResolveVirtual(
            address,
            length,
            MemoryMap::AccessType::Read,
            resolved);

    if(status != MemoryMap::Status::Ok)
    {
        BusResult result = MakeResult(BusStatus::MapFailure);

        result.mapStatus = status;

        return result;
    }

    return ReadResolved(
        resolved,
        MemoryMap::AccessType::Read,
        destination,
        length);
}

BusResult EE_MemoryBus::WriteVirtual(
    std::uint32_t address,
    const void* source,
    std::size_t length)
{
    if(!MemoryMap::EE_MemoryMap::IsValidAccessWidth(length))
    {
        return MakeResult(BusStatus::InvalidAccessWidth);
    }

    if(length != 0 && source == nullptr)
    {
        return MakeResult(BusStatus::InvalidArgument);
    }

    MemoryMap::ResolvedAddress resolved;

    const MemoryMap::Status status =
        MemoryMap::EE_MemoryMap::ResolveVirtual(
            address,
            length,
            MemoryMap::AccessType::Write,
            resolved);

    if(status != MemoryMap::Status::Ok)
    {
        BusResult result = MakeResult(BusStatus::MapFailure);

        result.mapStatus = status;

        return result;
    }

    return WriteResolved(
        resolved,
        source,
        length);
}

BusResult EE_MemoryBus::FetchVirtual(
    std::uint32_t address,
    void* destination,
    std::size_t length)
{
    if(!MemoryMap::EE_MemoryMap::IsValidAccessWidth(length))
    {
        return MakeResult(BusStatus::InvalidAccessWidth);
    }

    if(length != 0 && destination == nullptr)
    {
        return MakeResult(BusStatus::InvalidArgument);
    }

    MemoryMap::ResolvedAddress resolved;

    const MemoryMap::Status status =
        MemoryMap::EE_MemoryMap::ResolveVirtual(
            address,
            length,
            MemoryMap::AccessType::Execute,
            resolved);

    if(status != MemoryMap::Status::Ok)
    {
        BusResult result = MakeResult(BusStatus::MapFailure);

        result.mapStatus = status;

        return result;
    }

    return ReadResolved(
        resolved,
        MemoryMap::AccessType::Execute,
        destination,
        length);
}

// ------------------------------------------------------------
// PHYSICAL ACCESS
// ------------------------------------------------------------

BusResult EE_MemoryBus::ReadPhysical(
    std::uint32_t address,
    void* destination,
    std::size_t length)
{
    if(!MemoryMap::EE_MemoryMap::IsValidAccessWidth(length))
    {
        return MakeResult(BusStatus::InvalidAccessWidth);
    }

    if(length != 0 && destination == nullptr)
    {
        return MakeResult(BusStatus::InvalidArgument);
    }

    MemoryMap::ResolvedAddress resolved;

    const MemoryMap::Status status =
        MemoryMap::EE_MemoryMap::ResolvePhysical(
            address,
            length,
            MemoryMap::AccessType::Read,
            resolved);

    if(status != MemoryMap::Status::Ok)
    {
        BusResult result = MakeResult(BusStatus::MapFailure);

        result.mapStatus = status;

        return result;
    }

    return ReadResolved(
        resolved,
        MemoryMap::AccessType::Read,
        destination,
        length);
}

BusResult EE_MemoryBus::WritePhysical(
    std::uint32_t address,
    const void* source,
    std::size_t length)
{
    if(!MemoryMap::EE_MemoryMap::IsValidAccessWidth(length))
    {
        return MakeResult(BusStatus::InvalidAccessWidth);
    }

    if(length != 0 && source == nullptr)
    {
        return MakeResult(BusStatus::InvalidArgument);
    }

    MemoryMap::ResolvedAddress resolved;

    const MemoryMap::Status status =
        MemoryMap::EE_MemoryMap::ResolvePhysical(
            address,
            length,
            MemoryMap::AccessType::Write,
            resolved);

    if(status != MemoryMap::Status::Ok)
    {
        BusResult result = MakeResult(BusStatus::MapFailure);

        result.mapStatus = status;

        return result;
    }

    return WriteResolved(
        resolved,
        source,
        length);
}

// ------------------------------------------------------------
// DEVICE STATE
// ------------------------------------------------------------

bool EE_MemoryBus::IsDeviceAttached(
    MemoryMap::RegionId region) const
{
    if(!IsValidRegionId(region))
    {
        return false;
    }

    return m_devices[static_cast<std::size_t>(region)] != nullptr;
}

} // namespace Bus
} // namespace EE
} // namespace ORBIT2SX
