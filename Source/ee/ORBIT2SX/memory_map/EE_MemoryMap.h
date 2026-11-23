#pragma once

#include <cstddef>
#include <cstdint>

namespace ORBIT2SX
{
namespace EE
{
namespace MemoryMap
{

enum class RegionId : std::uint8_t
{
    MainRam = 0, EeMmio, Vu0MicroMemory, Vu0DataMemory,
    Vu1MicroMemory, Vu1DataMemory, GsPrivilegedRegisters,
    IopRam, BiosRom, Scratchpad
};

enum class MappingTarget : std::uint8_t
{
    EeBackingStore = 0,
    DeviceOwned
};

enum class BackingRegion : std::uint8_t
{
    None = 0,
    MainRam,
    Scratchpad,
    BiosRom
};

enum class AccessType : std::uint8_t
{
    Read = 1,
    Write = 2,
    Execute = 4
};

enum class Status : std::uint8_t
{
    Ok = 0,
    NeedsTlbTranslation,
    Unmapped,
    AccessCrossesRegion,
    PermissionDenied,
    InvalidAccessWidth,
    InvalidAccessType
};

struct RegionDescriptor
{
    std::uint32_t baseAddress;
    std::uint32_t sizeBytes;
    RegionId id;
    MappingTarget target;
    BackingRegion backingRegion;
    std::uint8_t permissions;
};

struct ResolvedAddress
{
    RegionId region;
    MappingTarget target;
    BackingRegion backingRegion;
    std::uint32_t physicalAddress;
    bool hasPhysicalAddress;
    std::uint32_t regionOffset;
    std::uint8_t permissions;
};

// Decodes direct-mapped EE segments and physical bus addresses.
// KUSEG, KSSEG and KSEG3 translation belongs to the future TLB layer.
class EE_MemoryMap final
{
public:
    EE_MemoryMap() = delete;

    static Status ResolveVirtual(
        std::uint32_t virtualAddress,
        std::size_t accessWidth,
        AccessType access,
        ResolvedAddress& result);

    static Status ResolvePhysical(
        std::uint32_t physicalAddress,
        std::size_t accessWidth,
        AccessType access,
        ResolvedAddress& result);

    static const RegionDescriptor* GetPhysicalRegions(
        std::size_t& count);

    static const RegionDescriptor* GetVirtualOnlyRegions(
        std::size_t& count);

    static const RegionDescriptor* FindRegion(RegionId id);

    static bool IsValidAccessWidth(std::size_t accessWidth);

private:
    static Status ResolveInRegion(
        const RegionDescriptor& descriptor,
        std::uint32_t address,
        std::size_t accessWidth,
        AccessType access,
        ResolvedAddress& result);
};

} // namespace MemoryMap
} // namespace EE
} // namespace ORBIT2SX
