#include "../EE_Memory.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

using ORBIT2SX::EE::Memory::Configuration;
using ORBIT2SX::EE::Memory::EE_Memory;
using ORBIT2SX::EE::Memory::Region;
using ORBIT2SX::EE::Memory::Status;
using ORBIT2SX::EE::Memory::UInt128;

namespace
{
int failures = 0;

void Check(bool condition, const char* name)
{
    if(!condition)
    {
        std::cerr << "FAIL: " << name << '\n';
        failures++;
    }
}

void TestLifecycle()
{
    EE_Memory memory;
    std::uint8_t value = 0;

    Check(
        memory.Read8(Region::MainRam, 0, value) == Status::NotInitialized,
        "read before initialize");

    Check(
        memory.Initialize(Configuration()) == Status::Ok,
        "initialize default profile");

    Check(memory.IsInitialized(), "initialized state");

    Configuration invalidConfiguration;
    invalidConfiguration.mainRamBytes = 0;

    Check(
        memory.Initialize(invalidConfiguration) == Status::InvalidArgument,
        "reject invalid reinitialization");

    Check(
        memory.IsInitialized(),
        "failed reinitialization preserves prior state");

    Check(
        memory.Read8(Region::MainRam, 0, value) == Status::Ok,
        "old memory remains accessible after failed reinitialization");

    ORBIT2SX::EE::Memory::RegionInfo invalidInfo;

    Check(
        memory.GetRegionInfo(static_cast<Region>(255), invalidInfo) == Status::InvalidRegion,
        "invalid region rejected");

    memory.Shutdown();

    Check(!memory.IsInitialized(), "shutdown state");
}

void TestWidthsAndEndianness()
{
    EE_Memory memory;

    Check(
        memory.Initialize(Configuration()) == Status::Ok,
        "initialize memory");

    Check(memory.IsInitialized(), "initialized state");

    Check(
        memory.Write16(Region::MainRam, 1, 0xA1B2) == Status::Ok,
        "unaligned backing write16 accepted");

    std::uint8_t bytes[2] = {0, 0};

    Check(
        memory.ReadBytes(Region::MainRam, 1, bytes, sizeof(bytes)) == Status::Ok,
        "read bytes");

    Check(
        bytes[0] == 0xB2 && bytes[1] == 0xA1,
        "little endian byte order");

    Check(
        memory.Write32(Region::MainRam, 4, 0x12345678) == Status::Ok,
        "write32");

    std::uint32_t word = 0;

    Check(
        memory.Read32(Region::MainRam, 4, word) == Status::Ok &&
        word == 0x12345678,
        "read32 round trip");

    const UInt128 input = {
        0x0123456789ABCDEFull,
        0xFEDCBA9876543210ull
    };

    UInt128 output = {0, 0};

    Check(
        memory.Write128(Region::MainRam, 16, input) == Status::Ok,
        "write128");

    Check(
        memory.Read128(Region::MainRam, 16, output) == Status::Ok,
        "read128");

    Check(
        memory.Write128(
            Region::MainRam,
            0x02000000 - 8,
            input) == Status::OutOfRange,
        "write128 crossing boundary rejected atomically");

    Check(
        output.low == input.low && output.high == input.high,
        "read128 round trip");
}

void TestBoundsAndReadOnly()
{
    EE_Memory memory;

    Check(
        memory.Initialize(Configuration()) == Status::Ok,
        "initialize bounds test memory");

    std::uint32_t value = 0xFFFFFFFF;

    Check(
        memory.Read32(
            Region::MainRam,
            0x02000000 - 4,
            value) == Status::Ok,
        "last valid word");

    Check(
        memory.Read32(
            Region::MainRam,
            0x02000000 - 3,
            value) == Status::OutOfRange,
        "cross-end word rejected");

    Check(
        memory.Write8(Region::BiosRom, 0, 0) == Status::ReadOnly,
        "bios write rejected");

    Check(
        memory.ReadBytes(Region::MainRam, 0, nullptr, 1) == Status::InvalidArgument,
        "null destination rejected");

    Check(
        memory.ReadBytes(
            Region::MainRam,
            0x02000000,
            nullptr,
            0) == Status::Ok,
        "zero length at end accepted");

    Check(
        memory.ReadBytes(
            Region::MainRam,
            0x02000001,
            nullptr,
            0) == Status::OutOfRange,
        "zero length beyond end rejected");
}

void TestCopyFillAndBios()
{
    EE_Memory memory;

    Check(
        memory.Initialize(Configuration()) == Status::Ok,
        "initialize copy test memory");

    const std::uint8_t data[] = {1, 2, 3, 4, 5, 6};

    Check(
        memory.WriteBytes(Region::MainRam, 32, data, sizeof(data)) == Status::Ok,
        "write byte block");

    Check(
        memory.Copy(Region::MainRam, 34, Region::MainRam, 32, 4) == Status::Ok,
        "overlap copy");

    std::uint8_t copied[6] = {0};

    Check(
        memory.ReadBytes(Region::MainRam, 32, copied, sizeof(copied)) == Status::Ok,
        "read copied block");

    const std::uint8_t expected[] = {1, 2, 1, 2, 3, 4};

    for(std::size_t i = 0; i < sizeof(expected); i++)
    {
        Check(copied[i] == expected[i], "memmove overlap semantics");
    }

    Check(
        memory.Fill(Region::Scratchpad, 0, 0x4000, 0x5A) == Status::Ok,
        "fill scratchpad");

    std::uint8_t scratch = 0;

    Check(
        memory.Read8(Region::Scratchpad, 0x3FFF, scratch) == Status::Ok &&
        scratch == 0x5A,
        "scratchpad last byte");

    Check(
        memory.Fill(Region::Scratchpad, 0x3FFF, 2, 0x00) == Status::OutOfRange,
        "scratchpad overflow rejected");

    std::vector<std::uint8_t> bios(0x00400000, 0x3C);

    Check(
        memory.LoadBios(bios.data(), bios.size()) == Status::Ok,
        "load exact bios image");

    std::uint8_t biosByte = 0;

    Check(
        memory.Read8(Region::BiosRom, bios.size() - 1, biosByte) == Status::Ok &&
        biosByte == 0x3C,
        "read bios last byte");

    Check(
        memory.LoadBios(bios.data(), bios.size() - 1) == Status::InvalidArgument,
        "reject partial bios image");
}
}

int main()
{
    TestLifecycle();
    TestWidthsAndEndianness();
    TestBoundsAndReadOnly();
    TestCopyFillAndBios();

    if(failures != 0)
    {
        std::cerr << failures << " test assertion(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All EE_Memory tests passed\n";
    return EXIT_SUCCESS;
}
