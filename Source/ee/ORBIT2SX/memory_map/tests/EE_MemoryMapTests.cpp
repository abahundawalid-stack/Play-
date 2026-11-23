#include "../EE_MemoryMap.h"

#include <cstdlib>
#include <iostream>

using namespace ORBIT2SX::EE::MemoryMap;

namespace
{
int failures = 0;

void Check(bool ok, const char* label)
{
    if(!ok)
    {
        std::cerr << "FAIL: " << label << '\n';
        ++failures;
    }
}

void TestPhysical()
{
    ResolvedAddress r;

    Check(
        EE_MemoryMap::ResolvePhysical(
            0, 4, AccessType::Read, r) == Status::Ok &&
        r.region == RegionId::MainRam &&
        r.regionOffset == 0,
        "main RAM base");

    Check(
        EE_MemoryMap::ResolvePhysical(
            0x01FFFFFCu, 4, AccessType::Write, r) == Status::Ok &&
        r.regionOffset == 0x01FFFFFCu,
        "main RAM final word");

    Check(
        EE_MemoryMap::ResolvePhysical(
            0x01FFFFFFu, 2, AccessType::Read, r) ==
            Status::AccessCrossesRegion,
        "main RAM boundary");

    Check(
        EE_MemoryMap::ResolvePhysical(
            0x10000000u, 4, AccessType::Read, r) == Status::Ok &&
        r.region == RegionId::EeMmio &&
        r.target == MappingTarget::DeviceOwned,
        "EE MMIO device route");

    Check(
        EE_MemoryMap::ResolvePhysical(
            0x11000000u, 4, AccessType::Write, r) == Status::Ok &&
        r.region == RegionId::Vu0MicroMemory &&
        r.target == MappingTarget::DeviceOwned,
        "VU0 device ownership");

    Check(
        EE_MemoryMap::ResolvePhysical(
            0x1100C000u, 16, AccessType::Read, r) == Status::Ok &&
        r.region == RegionId::Vu1DataMemory,
        "VU1 data memory");

    Check(
        EE_MemoryMap::ResolvePhysical(
            0x12000000u, 8, AccessType::Read, r) == Status::Ok &&
        r.region == RegionId::GsPrivilegedRegisters,
        "GS registers");

    Check(
        EE_MemoryMap::ResolvePhysical(
            0x1C000000u, 4, AccessType::Read, r) == Status::Ok &&
        r.region == RegionId::IopRam &&
        r.target == MappingTarget::DeviceOwned,
        "IOP RAM external ownership");

    Check(
        EE_MemoryMap::ResolvePhysical(
            0x1FC00000u, 4, AccessType::Execute, r) == Status::Ok &&
        r.region == RegionId::BiosRom,
        "BIOS executable");

    Check(
        EE_MemoryMap::ResolvePhysical(
            0x1FC00000u, 4, AccessType::Write, r) ==
            Status::PermissionDenied,
        "BIOS read only");

    Check(
        EE_MemoryMap::ResolvePhysical(
            0x02000000u, 4, AccessType::Read, r) ==
            Status::Unmapped,
        "unmapped gap");
}

void TestVirtual()
{
    ResolvedAddress r;

    Check(
        EE_MemoryMap::ResolveVirtual(
            0x80000020u, 4, AccessType::Read, r) == Status::Ok &&
        r.region == RegionId::MainRam &&
        r.physicalAddress == 0x20u,
        "KSEG0");

    Check(
        EE_MemoryMap::ResolveVirtual(
            0xA0000040u, 4, AccessType::Write, r) == Status::Ok &&
        r.region == RegionId::MainRam &&
        r.physicalAddress == 0x40u,
        "KSEG1");

    Check(
        EE_MemoryMap::ResolveVirtual(
            0x9FC00000u, 4, AccessType::Execute, r) == Status::Ok &&
        r.region == RegionId::BiosRom &&
        r.physicalAddress == 0x1FC00000u,
        "cached BIOS alias");

    Check(
        EE_MemoryMap::ResolveVirtual(
            0xBFC00000u, 4, AccessType::Read, r) == Status::Ok &&
        r.region == RegionId::BiosRom,
        "uncached BIOS alias");

    Check(
        EE_MemoryMap::ResolveVirtual(
            0x70000000u, 16, AccessType::Read, r) == Status::Ok &&
        r.region == RegionId::Scratchpad &&
        r.backingRegion == BackingRegion::Scratchpad &&
        !r.hasPhysicalAddress,
        "scratchpad virtual mapping");

    Check(
        EE_MemoryMap::ResolveVirtual(
            0x70003FFFu, 2, AccessType::Read, r) ==
            Status::AccessCrossesRegion,
        "scratchpad boundary");

    Check(
        EE_MemoryMap::ResolveVirtual(
            0x00001000u, 4, AccessType::Read, r) ==
            Status::NeedsTlbTranslation,
        "KUSEG TLB required");

    Check(
        EE_MemoryMap::ResolveVirtual(
            0xC0001000u, 4, AccessType::Read, r) ==
            Status::NeedsTlbTranslation,
        "KSSEG TLB required");

    Check(
        EE_MemoryMap::ResolveVirtual(
            0xE0001000u, 4, AccessType::Read, r) ==
            Status::NeedsTlbTranslation,
        "KSEG3 TLB required");

    Check(
        EE_MemoryMap::ResolveVirtual(
            0x9FFFFFFFu, 2, AccessType::Read, r) ==
            Status::AccessCrossesRegion,
        "KSEG boundary");
}

void TestMetadata()
{
    ResolvedAddress r;
    std::size_t n = 0;

    Check(
        EE_MemoryMap::ResolvePhysical(
            0, 3, AccessType::Read, r) ==
            Status::InvalidAccessWidth,
        "invalid width");

    Check(
        EE_MemoryMap::ResolvePhysical(
            0, 4, static_cast<AccessType>(0), r) ==
            Status::InvalidAccessType,
        "invalid access type");

    Check(
        EE_MemoryMap::GetPhysicalRegions(n) != nullptr &&
        n == 9,
        "physical descriptors");

    Check(
        EE_MemoryMap::GetVirtualOnlyRegions(n) != nullptr &&
        n == 1,
        "virtual-only descriptors");

    Check(
        EE_MemoryMap::FindRegion(RegionId::Scratchpad) != nullptr,
        "find scratchpad");

    Check(
        EE_MemoryMap::FindRegion(
            static_cast<RegionId>(255)) == nullptr,
        "unknown region");
}
}

int main()
{
    TestPhysical();
    TestVirtual();
    TestMetadata();

    if(failures)
    {
        std::cerr << failures << " assertion(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All EE_MemoryMap tests passed\n";
    return EXIT_SUCCESS;
}
