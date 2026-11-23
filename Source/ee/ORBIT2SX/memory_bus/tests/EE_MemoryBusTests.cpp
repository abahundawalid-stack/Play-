#include "../EE_MemoryBus.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

using namespace ORBIT2SX::EE;

namespace
{
int failures = 0;

void Check(bool condition, const char* label)
{
    if(!condition)
    {
        std::cerr << "FAIL: " << label << '\n';
        ++failures;
    }
}

class FakeDevice final : public Bus::IEEBusDevice
{
public:
    FakeDevice()
        : lastRegion(MemoryMap::RegionId::MainRam),
          lastOffset(0),
          reads(0),
          writes(0),
          nextStatus(Bus::DeviceStatus::Ok),
          bytes(0x10000, 0)
    {
    }

    Bus::DeviceStatus Read(
        MemoryMap::RegionId region,
        std::uint32_t offset,
        MemoryMap::AccessType,
        void* destination,
        std::size_t length) override
    {
        lastRegion = region;
        lastOffset = offset;
        ++reads;

        if(nextStatus != Bus::DeviceStatus::Ok)
        {
            return nextStatus;
        }

        if(destination == nullptr ||
           offset > bytes.size() ||
           length > bytes.size() - offset)
        {
            return Bus::DeviceStatus::AccessFault;
        }

        std::memcpy(
            destination,
            bytes.data() + offset,
            length);

        return Bus::DeviceStatus::Ok;
    }

    Bus::DeviceStatus Write(
        MemoryMap::RegionId region,
        std::uint32_t offset,
        const void* source,
        std::size_t length) override
    {
        lastRegion = region;
        lastOffset = offset;
        ++writes;

        if(nextStatus != Bus::DeviceStatus::Ok)
        {
            return nextStatus;
        }

        if(source == nullptr ||
           offset > bytes.size() ||
           length > bytes.size() - offset)
        {
            return Bus::DeviceStatus::AccessFault;
        }

        std::memcpy(
            bytes.data() + offset,
            source,
            length);

        return Bus::DeviceStatus::Ok;
    }

    MemoryMap::RegionId lastRegion;
    std::uint32_t lastOffset;

    unsigned reads;
    unsigned writes;

    Bus::DeviceStatus nextStatus;

    std::vector<std::uint8_t> bytes;
};

void TestBackingStore(Bus::EE_MemoryBus& bus)
{
    const std::uint32_t written = 0x12345678u;
    std::uint32_t read = 0;

    Bus::BusResult result = bus.WritePhysical(
        0x00000100u,
        &written,
        sizeof(written));

    Check(
        result.status == Bus::BusStatus::Ok,
        "main RAM physical write reaches EE_Memory");

    result = bus.ReadVirtual(
        0x80000100u,
        &read,
        sizeof(read));

    Check(
        result.status == Bus::BusStatus::Ok &&
        read == written,
        "KSEG0 read reaches same main RAM bytes");

    const std::uint32_t scratchValue = 0xAABBCCDDu;

    read = 0;

    result = bus.WriteVirtual(
        0x70000020u,
        &scratchValue,
        sizeof(scratchValue));

    Check(
        result.status == Bus::BusStatus::Ok,
        "scratchpad write dispatches to EE_Memory");

    result = bus.ReadVirtual(
        0x70000020u,
        &read,
        sizeof(read));

    Check(
        result.status == Bus::BusStatus::Ok &&
        read == scratchValue,
        "scratchpad readback");

    std::uint32_t biosInstruction = 0;

    result = bus.FetchVirtual(
        0xBFC00000u,
        &biosInstruction,
        sizeof(biosInstruction));

    Check(
        result.status == Bus::BusStatus::Ok &&
        biosInstruction == 0xFFFFFFFFu,
        "BIOS execute access is routed as a read");

    result = bus.WritePhysical(
        0x1FC00000u,
        &written,
        sizeof(written));

    Check(
        result.status == Bus::BusStatus::MapFailure &&
        result.mapStatus == MemoryMap::Status::PermissionDenied,
        "BIOS write rejected by map permissions");
}

void TestDeviceDispatch(Bus::EE_MemoryBus& bus)
{
    FakeDevice mmio;
    FakeDevice vu0;
    FakeDevice vu1;
    FakeDevice gs;
    FakeDevice iop;

    Check(
        bus.AttachDevice(
            MemoryMap::RegionId::EeMmio,
            &mmio).status == Bus::BusStatus::Ok,
        "attach EE MMIO endpoint");

    Check(
        bus.AttachDevice(
            MemoryMap::RegionId::Vu0MicroMemory,
            &vu0).status == Bus::BusStatus::Ok,
        "attach VU0 micro endpoint");

    Check(
        bus.AttachDevice(
            MemoryMap::RegionId::Vu0DataMemory,
            &vu0).status == Bus::BusStatus::Ok,
        "share VU0 endpoint with data region");

    Check(
        bus.AttachDevice(
            MemoryMap::RegionId::Vu1MicroMemory,
            &vu1).status == Bus::BusStatus::Ok,
        "attach VU1 micro endpoint");

    Check(
        bus.AttachDevice(
            MemoryMap::RegionId::Vu1DataMemory,
            &vu1).status == Bus::BusStatus::Ok,
        "share VU1 endpoint with data region");

    Check(
        bus.AttachDevice(
            MemoryMap::RegionId::GsPrivilegedRegisters,
            &gs).status == Bus::BusStatus::Ok,
        "attach GS endpoint");

    Check(
        bus.AttachDevice(
            MemoryMap::RegionId::IopRam,
            &iop).status == Bus::BusStatus::Ok,
        "attach IOP endpoint");

    const std::uint32_t value = 0xCAFEBABEu;
    std::uint32_t read = 0;

    Bus::BusResult result = bus.WritePhysical(
        0x10000040u,
        &value,
        sizeof(value));

    Check(
        result.status == Bus::BusStatus::Ok &&
        mmio.writes == 1 &&
        mmio.lastOffset == 0x40u,
        "MMIO write dispatch and offset");

    result = bus.ReadPhysical(
        0x10000040u,
        &read,
        sizeof(read));

    Check(
        result.status == Bus::BusStatus::Ok &&
        read == value &&
        mmio.reads == 1,
        "MMIO read dispatch");

    result = bus.WritePhysical(
        0x11004010u,
        &value,
        sizeof(value));

    Check(
        result.status == Bus::BusStatus::Ok &&
        vu0.lastRegion == MemoryMap::RegionId::Vu0DataMemory &&
        vu0.lastOffset == 0x10u,
        "VU0 data memory routes to VU0 endpoint");

    result = bus.WritePhysical(
        0x11008020u,
        &value,
        sizeof(value));

    Check(
        result.status == Bus::BusStatus::Ok &&
        vu1.lastRegion == MemoryMap::RegionId::Vu1MicroMemory,
        "VU1 micro memory routes to VU1 endpoint");

    result = bus.WritePhysical(
        0x1100C020u,
        &value,
        sizeof(value));

    Check(
        result.status == Bus::BusStatus::Ok &&
        vu1.lastRegion == MemoryMap::RegionId::Vu1DataMemory,
        "VU1 data memory routes to VU1 endpoint");

    result = bus.WritePhysical(
        0x12000080u,
        &value,
        sizeof(value));

    Check(
        result.status == Bus::BusStatus::Ok &&
        gs.lastOffset == 0x80u,
        "GS privileged register dispatch");

    result = bus.WritePhysical(
        0x1C000100u,
        &value,
        sizeof(value));

    Check(
        result.status == Bus::BusStatus::Ok &&
        iop.lastOffset == 0x100u,
        "IOP RAM dispatch");

    mmio.nextStatus = Bus::DeviceStatus::Busy;

    result = bus.ReadPhysical(
        0x10000040u,
        &read,
        sizeof(read));

    Check(
        result.status == Bus::BusStatus::DeviceFailure &&
        result.deviceStatus == Bus::DeviceStatus::Busy,
        "device error is propagated without being hidden");

    Check(
        bus.DetachDevice(
            MemoryMap::RegionId::EeMmio).status == Bus::BusStatus::Ok,
        "detach MMIO endpoint");

    result = bus.ReadPhysical(
        0x10000040u,
        &read,
        sizeof(read));

    Check(
        result.status == Bus::BusStatus::DeviceNotAttached,
        "unattached device is reported");

    Check(
        bus.AttachDevice(
            MemoryMap::RegionId::MainRam,
            &mmio).status == Bus::BusStatus::InvalidArgument,
        "backing-store region cannot be overridden by a device");

    bus.DetachAllDevices();
}

void TestFailuresAndResolvedAccess(Bus::EE_MemoryBus& bus)
{
    std::uint32_t value = 0;

    Bus::BusResult result = bus.ReadVirtual(
        0x00001000u,
        &value,
        sizeof(value));

    Check(
        result.status == Bus::BusStatus::MapFailure &&
        result.mapStatus == MemoryMap::Status::NeedsTlbTranslation,
        "KUSEG is not translated by the bus");

    result = bus.ReadPhysical(
        0x02000000u,
        &value,
        sizeof(value));

    Check(
        result.status == Bus::BusStatus::MapFailure &&
        result.mapStatus == MemoryMap::Status::Unmapped,
        "unmapped physical address reported");

    result = bus.ReadPhysical(
        0,
        nullptr,
        sizeof(value));

    Check(
        result.status == Bus::BusStatus::InvalidArgument,
        "null read destination rejected");

    result = bus.ReadPhysical(
        0,
        &value,
        3);

    Check(
        result.status == Bus::BusStatus::InvalidAccessWidth,
        "invalid width rejected by bus API");

    MemoryMap::ResolvedAddress resolved;

    Check(
        MemoryMap::EE_MemoryMap::ResolvePhysical(
            0,
            sizeof(value),
            MemoryMap::AccessType::Read,
            resolved) == MemoryMap::Status::Ok,
        "resolve physical address for direct API");

    resolved.regionOffset = 0x01FFFFFFu;

    result = bus.ReadResolved(
        resolved,
        MemoryMap::AccessType::Read,
        &value,
        sizeof(value));

    Check(
        result.status == Bus::BusStatus::MapFailure,
        "forged or inconsistent resolved address rejected");

    result = bus.DetachDevice(
        static_cast<MemoryMap::RegionId>(255));

    Check(
        result.status == Bus::BusStatus::InvalidArgument,
        "invalid device region rejected");

    result = bus.DetachDevice(
        MemoryMap::RegionId::MainRam);

    Check(
        result.status == Bus::BusStatus::InvalidArgument,
        "backing-store region cannot be detached as a device");
}
}

int main()
{
    Memory::Configuration configuration;

    configuration.mainRamBytes = 0x02000000;
    configuration.biosRomBytes = 0x00400000;

    Memory::EE_Memory memory;

    Check(
        memory.Initialize(configuration) == Memory::Status::Ok,
        "initialize test backing store");

    Bus::EE_MemoryBus bus(memory);

    TestBackingStore(bus);
    TestDeviceDispatch(bus);
    TestFailuresAndResolvedAccess(bus);

    if(failures != 0)
    {
        std::cerr << failures << " test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All EE_MemoryBus tests passed\n";

    return EXIT_SUCCESS;
}
