#include "EE_MemoryMap.h"

namespace ORBIT2SX
{
namespace EE
{
namespace MemoryMap
{
namespace
{
const std::uint8_t kRead =
    static_cast<std::uint8_t>(AccessType::Read);

const std::uint8_t kWrite =
    static_cast<std::uint8_t>(AccessType::Write);

const std::uint8_t kExecute =
    static_cast<std::uint8_t>(AccessType::Execute);

const std::uint8_t kReadWrite = kRead | kWrite;
const std::uint8_t kReadWriteExecute = kRead | kWrite | kExecute;
const std::uint8_t kReadExecute = kRead | kExecute;

const RegionDescriptor kPhysicalRegions[] =
{
    // Main EE RDRAM: 32 MiB.
    {
        0x00000000u, 0x02000000u,
        RegionId::MainRam,
        MappingTarget::EeBackingStore,
        BackingRegion::MainRam,
        kReadWriteExecute
    },

    // EE hardware registers: 64 KiB.
    {
        0x10000000u, 0x00010000u,
        RegionId::EeMmio,
        MappingTarget::DeviceOwned,
        BackingRegion::None,
        kReadWrite
    },

    // VU0 micro memory.
    {
        0x11000000u, 0x00001000u,
        RegionId::Vu0MicroMemory,
        MappingTarget::DeviceOwned,
        BackingRegion::None,
        kReadWrite
    },

    // VU0 data memory.
    {
        0x11004000u, 0x00001000u,
        RegionId::Vu0DataMemory,
        MappingTarget::DeviceOwned,
        BackingRegion::None,
        kReadWrite
    },

    // VU1 micro memory.
    {
        0x11008000u, 0x00004000u,
        RegionId::Vu1MicroMemory,
        MappingTarget::DeviceOwned,
        BackingRegion::None,
        kReadWrite
    },

    // VU1 data memory.
    {
        0x1100C000u, 0x00004000u,
        RegionId::Vu1DataMemory,
        MappingTarget::DeviceOwned,
        BackingRegion::None,
        kReadWrite
    },

    // GS privileged registers: 8 KiB.
    {
        0x12000000u, 0x00002000u,
        RegionId::GsPrivilegedRegisters,
        MappingTarget::DeviceOwned,
        BackingRegion::None,
        kReadWrite
    },

    // IOP RAM: owned by the IOP subsystem.
    {
        0x1C000000u, 0x00200000u,
        RegionId::IopRam,
        MappingTarget::DeviceOwned,
        BackingRegion::None,
        kReadWrite
    },

    // BIOS ROM0: read and execute, never write.
    {
        0x1FC00000u, 0x00400000u,
        RegionId::BiosRom,
        MappingTarget::EeBackingStore,
        BackingRegion::BiosRom,
        kReadExecute
    }
};

// Scratchpad is virtual-only.
// It has no physical bus address in this map.
const RegionDescriptor kVirtualOnlyRegions[] =
{
    {
        0x70000000u, 0x00004000u,
        RegionId::Scratchpad,
        MappingTarget::EeBackingStore,
        BackingRegion::Scratchpad,
        kReadWrite
    }
};

bool IsValidAccessType(AccessType access)
{
    return access == AccessType::Read ||
           access == AccessType::Write ||
           access == AccessType::Execute;
}

bool HasPermission(
    std::uint8_t permissions,
    AccessType access)
{
    const std::uint8_t requested =
        static_cast<std::uint8_t>(access);

    return (permissions & requested) == requested;
}

bool IsRangeInside(
    std::uint32_t base,
    std::uint32_t size,
    std::uint32_t address,
    std::size_t width)
{
    if(address < base) return false;

    const std::uint64_t offset =
        static_cast<std::uint64_t>(address) - base;

    const std::uint64_t end =
        offset + static_cast<std::uint64_t>(width);

    return end <= static_cast<std::uint64_t>(size);
}
}

bool EE_MemoryMap::IsValidAccessWidth(std::size_t width)
{
    return width == 1 ||
           width == 2 ||
           width == 4 ||
           width == 8 ||
           width == 16;
}

const RegionDescriptor*
EE_MemoryMap::GetPhysicalRegions(std::size_t& count)
{
    count = sizeof(kPhysicalRegions) /
            sizeof(kPhysicalRegions[0]);

    return kPhysicalRegions;
}

const RegionDescriptor*
EE_MemoryMap::GetVirtualOnlyRegions(std::size_t& count)
{
    count = sizeof(kVirtualOnlyRegions) /
            sizeof(kVirtualOnlyRegions[0]);

    return kVirtualOnlyRegions;
}

const RegionDescriptor*
EE_MemoryMap::FindRegion(RegionId id)
{
    std::size_t count = 0;

    const RegionDescriptor* regions =
        GetPhysicalRegions(count);

    for(std::size_t i = 0; i < count; ++i)
    {
        if(regions[i].id == id)
        {
            return &regions[i];
        }
    }

    regions = GetVirtualOnlyRegions(count);

    for(std::size_t i = 0; i < count; ++i)
    {
        if(regions[i].id == id)
        {
            return &regions[i];
        }
    }

    return nullptr;
}

Status EE_MemoryMap::ResolveInRegion(
    const RegionDescriptor& descriptor,
    std::uint32_t address,
    std::size_t width,
    AccessType access,
    ResolvedAddress& result)
{
    if(!IsRangeInside(
        descriptor.baseAddress,
        descriptor.sizeBytes,
        address,
        width))
    {
        return Status::AccessCrossesRegion;
    }

    if(!HasPermission(descriptor.permissions, access))
    {
        return Status::PermissionDenied;
    }

    ResolvedAddress resolved;

    resolved.region = descriptor.id;
    resolved.target = descriptor.target;
    resolved.backingRegion = descriptor.backingRegion;
    resolved.physicalAddress = address;
    resolved.hasPhysicalAddress = true;
    resolved.regionOffset = address - descriptor.baseAddress;
    resolved.permissions = descriptor.permissions;

    result = resolved;

    return Status::Ok;
}

Status EE_MemoryMap::ResolvePhysical(
    std::uint32_t address,
    std::size_t width,
    AccessType access,
    ResolvedAddress& result)
{
    if(!IsValidAccessWidth(width))
    {
        return Status::InvalidAccessWidth;
    }

    if(!IsValidAccessType(access))
    {
        return Status::InvalidAccessType;
    }

    std::size_t count = 0;

    const RegionDescriptor* regions =
        GetPhysicalRegions(count);

    for(std::size_t i = 0; i < count; ++i)
    {
        const RegionDescriptor& region = regions[i];

        const std::uint64_t end =
            static_cast<std::uint64_t>(region.baseAddress) +
            region.sizeBytes;

        if(address >= region.baseAddress &&
           static_cast<std::uint64_t>(address) < end)
        {
            return ResolveInRegion(
                region,
                address,
                width,
                access,
                result);
        }
    }

    return Status::Unmapped;
}

Status EE_MemoryMap::ResolveVirtual(
    std::uint32_t address,
    std::size_t width,
    AccessType access,
    ResolvedAddress& result)
{
    if(!IsValidAccessWidth(width))
    {
        return Status::InvalidAccessWidth;
    }

    if(!IsValidAccessType(access))
    {
        return Status::InvalidAccessType;
    }

    // Scratchpad has a dedicated virtual address window.
    std::size_t count = 0;

    const RegionDescriptor* virtualRegions =
        GetVirtualOnlyRegions(count);

    for(std::size_t i = 0; i < count; ++i)
    {
        const RegionDescriptor& region = virtualRegions[i];

        const std::uint64_t end =
            static_cast<std::uint64_t>(region.baseAddress) +
            region.sizeBytes;

        if(address >= region.baseAddress &&
           static_cast<std::uint64_t>(address) < end)
        {
            if(!IsRangeInside(
                region.baseAddress,
                region.sizeBytes,
                address,
                width))
            {
                return Status::AccessCrossesRegion;
            }

            if(!HasPermission(region.permissions, access))
            {
                return Status::PermissionDenied;
            }

            ResolvedAddress resolved;

            resolved.region = region.id;
            resolved.target = region.target;
            resolved.backingRegion = region.backingRegion;

            // Scratchpad has no physical bus address.
            resolved.physicalAddress = 0;
            resolved.hasPhysicalAddress = false;

            resolved.regionOffset =
                address - region.baseAddress;

            resolved.permissions = region.permissions;

            result = resolved;

            return Status::Ok;
        }
    }

    // KSEG0: cached direct-mapped segment.
    const bool kseg0 =
        address >= 0x80000000u &&
        address <= 0x9FFFFFFFu;

    // KSEG1: uncached direct-mapped segment.
    const bool kseg1 =
        address >= 0xA0000000u &&
        address <= 0xBFFFFFFFu;

    if(kseg0 || kseg1)
    {
        const std::uint64_t segmentEnd =
            kseg0 ? 0xA0000000ull : 0xC0000000ull;

        if(static_cast<std::uint64_t>(address) + width >
           segmentEnd)
        {
            return Status::AccessCrossesRegion;
        }

        const std::uint32_t physicalAddress =
            address & 0x1FFFFFFFu;

        return ResolvePhysical(
            physicalAddress,
            width,
            access,
            result);
    }

    // KUSEG, KSSEG and KSEG3 need TLB/COP0 translation.
    return Status::NeedsTlbTranslation;
}

} // namespace MemoryMap
} // namespace EE
} // namespace ORBIT2SX
