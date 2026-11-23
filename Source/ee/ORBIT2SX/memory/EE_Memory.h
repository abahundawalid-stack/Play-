#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ORBIT2SX
{
namespace EE
{
namespace Memory
{

enum class Region : std::uint8_t
{
    MainRam = 0,
    Scratchpad,
    BiosRom,
    Count
};

enum class Status : std::uint8_t
{
    Ok = 0,
    NotInitialized,
    InvalidRegion,
    InvalidArgument,
    OutOfRange,
    ReadOnly,
    AllocationFailure
};

struct Configuration
{
    std::size_t mainRamBytes;
    std::size_t biosRomBytes;

    Configuration();
};

struct UInt128
{
    std::uint64_t low;
    std::uint64_t high;
};

struct RegionInfo
{
    std::size_t sizeBytes;
    bool writable;
};

// Owns EE backing stores only.
// VU local memory remains owned by its VU device.
// External synchronization is required for concurrent access.
class EE_Memory final
{
public:
    EE_Memory();
    explicit EE_Memory(const Configuration& configuration);

    EE_Memory(const EE_Memory&) = delete;
    EE_Memory& operator=(const EE_Memory&) = delete;

    Status Initialize(const Configuration& configuration);
    void Shutdown();
    bool IsInitialized() const;

    Status GetRegionInfo(Region region, RegionInfo& info) const;

    // CPU alignment and architectural exceptions belong to
    // the transaction/CPU layer, not this byte-addressable store.
    Status ReadBytes(Region region, std::size_t offset, void* destination, std::size_t length) const;
    Status WriteBytes(Region region, std::size_t offset, const void* source, std::size_t length);
    Status Fill(Region region, std::size_t offset, std::size_t length, std::uint8_t value);

    Status Copy(
        Region destinationRegion,
        std::size_t destinationOffset,
        Region sourceRegion,
        std::size_t sourceOffset,
        std::size_t length);

    Status Read8(Region region, std::size_t offset, std::uint8_t& value) const;
    Status Read16(Region region, std::size_t offset, std::uint16_t& value) const;
    Status Read32(Region region, std::size_t offset, std::uint32_t& value) const;
    Status Read64(Region region, std::size_t offset, std::uint64_t& value) const;
    Status Read128(Region region, std::size_t offset, UInt128& value) const;

    Status Write8(Region region, std::size_t offset, std::uint8_t value);
    Status Write16(Region region, std::size_t offset, std::uint16_t value);
    Status Write32(Region region, std::size_t offset, std::uint32_t value);
    Status Write64(Region region, std::size_t offset, std::uint64_t value);
    Status Write128(Region region, std::size_t offset, const UInt128& value);

    Status LoadBios(const void* source, std::size_t length);
    Status ClearWritableRegions(std::uint8_t value);

private:
    static const std::size_t RegionCount =
        static_cast<std::size_t>(Region::Count);

    static bool IsValidRegion(Region region);
    static bool IsWritableRegion(Region region);

    static bool IsRangeValid(
        std::size_t capacity,
        std::size_t offset,
        std::size_t length);

    const std::vector<std::uint8_t>* FindRegion(Region region) const;
    std::vector<std::uint8_t>* FindRegion(Region region);

    template <typename T>
    Status ReadLittleEndian(
        Region region,
        std::size_t offset,
        T& value) const;

    template <typename T>
    Status WriteLittleEndian(
        Region region,
        std::size_t offset,
        T value);

    std::array<std::vector<std::uint8_t>, RegionCount> m_regions;
    bool m_initialized;
};

} // namespace Memory
} // namespace EE
} // namespace ORBIT2SX
