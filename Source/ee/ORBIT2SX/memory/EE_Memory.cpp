#include "EE_Memory.h"

#include <algorithm>
#include <cstring>
#include <new>
#include <stdexcept>
#include <utility>

namespace ORBIT2SX
{
namespace EE
{
namespace Memory
{

namespace
{
const std::size_t kDefaultMainRamBytes = 0x02000000;
const std::size_t kDefaultBiosRomBytes = 0x00400000;
const std::size_t kScratchpadBytes = 0x00004000;

std::size_t ToIndex(Region region)
{
    return static_cast<std::size_t>(region);
}
}

Configuration::Configuration()
    : mainRamBytes(kDefaultMainRamBytes)
    , biosRomBytes(kDefaultBiosRomBytes)
{
}

EE_Memory::EE_Memory()
    : m_initialized(false)
{
}

EE_Memory::EE_Memory(const Configuration& configuration)
    : m_initialized(false)
{
    Initialize(configuration);
}

Status EE_Memory::Initialize(const Configuration& configuration)
{
    if(configuration.mainRamBytes == 0 || configuration.biosRomBytes == 0)
    {
        return Status::InvalidArgument;
    }

    std::array<std::vector<std::uint8_t>, RegionCount> newRegions;

    try
    {
        newRegions[ToIndex(Region::MainRam)].assign(
            configuration.mainRamBytes, 0);

        newRegions[ToIndex(Region::Scratchpad)].assign(
            kScratchpadBytes, 0);

        newRegions[ToIndex(Region::BiosRom)].assign(
            configuration.biosRomBytes, 0xFF);
    }
    catch(const std::bad_alloc&)
    {
        return Status::AllocationFailure;
    }
    catch(const std::length_error&)
    {
        return Status::InvalidArgument;
    }

    m_regions.swap(newRegions);
    m_initialized = true;

    return Status::Ok;
}

void EE_Memory::Shutdown()
{
    for(std::size_t i = 0; i < RegionCount; i++)
    {
        std::vector<std::uint8_t>().swap(m_regions[i]);
    }

    m_initialized = false;
}

bool EE_Memory::IsInitialized() const
{
    return m_initialized;
}

bool EE_Memory::IsValidRegion(Region region)
{
    return ToIndex(region) < RegionCount;
}

bool EE_Memory::IsWritableRegion(Region region)
{
    return region != Region::BiosRom;
}

bool EE_Memory::IsRangeValid(
    std::size_t capacity,
    std::size_t offset,
    std::size_t length)
{
    return offset <= capacity &&
           length <= (capacity - offset);
}

const std::vector<std::uint8_t>* EE_Memory::FindRegion(
    Region region) const
{
    if(!IsValidRegion(region)) return nullptr;

    return &m_regions[ToIndex(region)];
}

std::vector<std::uint8_t>* EE_Memory::FindRegion(
    Region region)
{
    if(!IsValidRegion(region)) return nullptr;

    return &m_regions[ToIndex(region)];
}

Status EE_Memory::GetRegionInfo(
    Region region,
    RegionInfo& info) const
{
    if(!m_initialized) return Status::NotInitialized;

    const auto* memory = FindRegion(region);

    if(memory == nullptr) return Status::InvalidRegion;

    info.sizeBytes = memory->size();
    info.writable = IsWritableRegion(region);

    return Status::Ok;
}

Status EE_Memory::ReadBytes(
    Region region,
    std::size_t offset,
    void* destination,
    std::size_t length) const
{
    if(!m_initialized) return Status::NotInitialized;

    const auto* memory = FindRegion(region);

    if(memory == nullptr) return Status::InvalidRegion;

    if(length != 0 && destination == nullptr)
    {
        return Status::InvalidArgument;
    }

    if(!IsRangeValid(memory->size(), offset, length))
    {
        return Status::OutOfRange;
    }

    if(length != 0)
    {
        std::memcpy(
            destination,
            memory->data() + offset,
            length);
    }

    return Status::Ok;
}

Status EE_Memory::WriteBytes(
    Region region,
    std::size_t offset,
    const void* source,
    std::size_t length)
{
    if(!m_initialized) return Status::NotInitialized;

    std::vector<std::uint8_t>* memory = FindRegion(region);

    if(memory == nullptr) return Status::InvalidRegion;

    if(!IsWritableRegion(region)) return Status::ReadOnly;

    if(length != 0 && source == nullptr)
    {
        return Status::InvalidArgument;
    }

    if(!IsRangeValid(memory->size(), offset, length))
    {
        return Status::OutOfRange;
    }

    if(length != 0)
    {
        std::memcpy(
            memory->data() + offset,
            source,
            length);
    }

    return Status::Ok;
}

Status EE_Memory::Fill(
    Region region,
    std::size_t offset,
    std::size_t length,
    std::uint8_t value)
{
    if(!m_initialized) return Status::NotInitialized;

    std::vector<std::uint8_t>* memory = FindRegion(region);

    if(memory == nullptr) return Status::InvalidRegion;

    if(!IsWritableRegion(region)) return Status::ReadOnly;

    if(!IsRangeValid(memory->size(), offset, length))
    {
        return Status::OutOfRange;
    }

    if(length != 0)
    {
        std::fill_n(memory->data() + offset, length, value);
    }

    return Status::Ok;
}

Status EE_Memory::Copy(
    Region destinationRegion,
    std::size_t destinationOffset,
    Region sourceRegion,
    std::size_t sourceOffset,
    std::size_t length)
{
    if(!m_initialized) return Status::NotInitialized;

    std::vector<std::uint8_t>* destination =
        FindRegion(destinationRegion);

    const std::vector<std::uint8_t>* source =
        FindRegion(sourceRegion);

    if(destination == nullptr || source == nullptr)
    {
        return Status::InvalidRegion;
    }

    if(!IsWritableRegion(destinationRegion))
    {
        return Status::ReadOnly;
    }

    if(!IsRangeValid(source->size(), sourceOffset, length) ||
       !IsRangeValid(destination->size(), destinationOffset, length))
    {
        return Status::OutOfRange;
    }

    if(length != 0)
    {
        if(destinationRegion == sourceRegion)
        {
            std::memmove(
                destination->data() + destinationOffset,
                source->data() + sourceOffset,
                length);
        }
        else
        {
            std::memcpy(
                destination->data() + destinationOffset,
                source->data() + sourceOffset,
                length);
        }
    }

    return Status::Ok;
}

template <typename T>
Status EE_Memory::ReadLittleEndian(
    Region region,
    std::size_t offset,
    T& value) const
{
    std::uint8_t bytes[sizeof(T)];

    const Status status =
        ReadBytes(region, offset, bytes, sizeof(T));

    if(status != Status::Ok) return status;

    T result = 0;

    for(std::size_t i = 0; i < sizeof(T); i++)
    {
        result |= static_cast<T>(bytes[i]) << (i * 8);
    }

    value = result;

    return Status::Ok;
}

template <typename T>
Status EE_Memory::WriteLittleEndian(
    Region region,
    std::size_t offset,
    T value)
{
    std::uint8_t bytes[sizeof(T)];

    for(std::size_t i = 0; i < sizeof(T); i++)
    {
        bytes[i] = static_cast<std::uint8_t>(
            (value >> (i * 8)) & static_cast<T>(0xFF));
    }

    return WriteBytes(region, offset, bytes, sizeof(T));
}

Status EE_Memory::Read8(
    Region region,
    std::size_t offset,
    std::uint8_t& value) const
{
    return ReadLittleEndian(region, offset, value);
}

Status EE_Memory::Read16(
    Region region,
    std::size_t offset,
    std::uint16_t& value) const
{
    return ReadLittleEndian(region, offset, value);
}

Status EE_Memory::Read32(
    Region region,
    std::size_t offset,
    std::uint32_t& value) const
{
    return ReadLittleEndian(region, offset, value);
}

Status EE_Memory::Read64(
    Region region,
    std::size_t offset,
    std::uint64_t& value) const
{
    return ReadLittleEndian(region, offset, value);
}

Status EE_Memory::Read128(
    Region region,
    std::size_t offset,
    UInt128& value) const
{
    std::uint8_t bytes[16];

    const Status status =
        ReadBytes(region, offset, bytes, sizeof(bytes));

    if(status != Status::Ok) return status;

    UInt128 result = {0, 0};

    for(std::size_t i = 0; i < 8; i++)
    {
        result.low |=
            static_cast<std::uint64_t>(bytes[i]) << (i * 8);

        result.high |=
            static_cast<std::uint64_t>(bytes[i + 8]) << (i * 8);
    }

    value = result;

    return Status::Ok;
}

Status EE_Memory::Write8(
    Region region,
    std::size_t offset,
    std::uint8_t value)
{
    return WriteLittleEndian(region, offset, value);
}

Status EE_Memory::Write16(
    Region region,
    std::size_t offset,
    std::uint16_t value)
{
    return WriteLittleEndian(region, offset, value);
}

Status EE_Memory::Write32(
    Region region,
    std::size_t offset,
    std::uint32_t value)
{
    return WriteLittleEndian(region, offset, value);
}

Status EE_Memory::Write64(
    Region region,
    std::size_t offset,
    std::uint64_t value)
{
    return WriteLittleEndian(region, offset, value);
}

Status EE_Memory::Write128(
    Region region,
    std::size_t offset,
    const UInt128& value)
{
    std::uint8_t bytes[16];

    for(std::size_t i = 0; i < 8; i++)
    {
        bytes[i] = static_cast<std::uint8_t>(
            (value.low >> (i * 8)) & 0xFF);

        bytes[i + 8] = static_cast<std::uint8_t>(
            (value.high >> (i * 8)) & 0xFF);
    }

    return WriteBytes(region, offset, bytes, sizeof(bytes));
}

Status EE_Memory::LoadBios(
    const void* source,
    std::size_t length)
{
    if(!m_initialized) return Status::NotInitialized;

    std::vector<std::uint8_t>* bios =
        FindRegion(Region::BiosRom);

    if(bios == nullptr) return Status::InvalidRegion;

    if(source == nullptr || length != bios->size())
    {
        return Status::InvalidArgument;
    }

    std::memcpy(bios->data(), source, length);

    return Status::Ok;
}

Status EE_Memory::ClearWritableRegions(std::uint8_t value)
{
    if(!m_initialized) return Status::NotInitialized;

    for(std::size_t i = 0; i < RegionCount; i++)
    {
        const Region region = static_cast<Region>(i);

        if(IsWritableRegion(region))
        {
            std::fill(
                m_regions[i].begin(),
                m_regions[i].end(),
                value);
        }
    }

    return Status::Ok;
}

} // namespace Memory
} // namespace EE
} // namespace ORBIT2SX
