#include "../EE_DeviceRegistry.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

using namespace ORBIT2SX::EE;

namespace
{

class TestDevice final : public DeviceRegistry::IEEDevice
{
public:
    TestDevice(
        int deviceId,
        std::vector<int>& events,
        bool failInitialize = false)
        : m_deviceId(deviceId),
          m_events(events),
          m_failInitialize(failInitialize),
          m_initialized(false)
    {
    }

    Bus::DeviceStatus Initialize() override
    {
        m_events.push_back(m_deviceId * 10 + 1);

        if(m_failInitialize)
        {
            return Bus::DeviceStatus::InternalError;
        }

        m_initialized = true;
        return Bus::DeviceStatus::Ok;
    }

    Bus::DeviceStatus Reset() override
    {
        if(!m_initialized)
        {
            return Bus::DeviceStatus::AccessFault;
        }

        m_events.push_back(m_deviceId * 10 + 2);
        return Bus::DeviceStatus::Ok;
    }

    Bus::DeviceStatus Shutdown() override
    {
        m_events.push_back(m_deviceId * 10 + 3);
        m_initialized = false;
        return Bus::DeviceStatus::Ok;
    }

    Bus::DeviceStatus Read(
        MemoryMap::RegionId,
        std::uint32_t,
        MemoryMap::AccessType,
        void* destination,
        std::size_t length) override
    {
        if(destination == nullptr || length == 0)
        {
            return Bus::DeviceStatus::AccessFault;
        }

        return Bus::DeviceStatus::Ok;
    }

    Bus::DeviceStatus Write(
        MemoryMap::RegionId,
        std::uint32_t,
        const void* source,
        std::size_t length) override
    {
        if(source == nullptr || length == 0)
        {
            return Bus::DeviceStatus::AccessFault;
        }

        return Bus::DeviceStatus::Ok;
    }

private:
    int m_deviceId;
    std::vector<int>& m_events;
    bool m_failInitialize;
    bool m_initialized;
};

DeviceRegistry::DeviceRegistration MakeRegistration(
    DeviceRegistry::DeviceId id,
    const char* name,
    DeviceRegistry::IEEDevice* device,
    MemoryMap::RegionId region)
{
    DeviceRegistry::DeviceRegistration registration;
    registration.id = id;
    registration.name = name;
    registration.device = device;
    registration.regions.push_back(region);
    return registration;
}

void TestDependencyOrderAndLifecycle()
{
    Memory::EE_Memory memory;
    Memory::Configuration configuration;

    assert(memory.Initialize(configuration) == Memory::Status::Ok);

    Bus::EE_MemoryBus bus(memory);
    DeviceRegistry::EE_DeviceRegistry registry(bus);

    std::vector<int> events;

    TestDevice baseDevice(1, events);
    TestDevice dependentDevice(2, events);

    // Register the dependent first to prove that registration order
    // does not determine initialization order.
    DeviceRegistry::DeviceRegistration dependent =
        MakeRegistration(
            2,
            "DependentDevice",
            &dependentDevice,
            MemoryMap::RegionId::GsPrivilegedRegisters);

    dependent.dependencies.push_back(1);

    assert(
        registry.RegisterDevice(dependent).status ==
        DeviceRegistry::DeviceStatus::Ok);

    DeviceRegistry::DeviceRegistration base =
        MakeRegistration(
            1,
            "BaseDevice",
            &baseDevice,
            MemoryMap::RegionId::EeMmio);

    assert(
        registry.RegisterDevice(base).status ==
        DeviceRegistry::DeviceStatus::Ok);

    assert(registry.GetDeviceCount() == 2);
    assert(registry.ContainsDevice(1));
    assert(registry.ContainsDevice(2));

    assert(
        registry.Start().status ==
        DeviceRegistry::DeviceStatus::Ok);

    assert(registry.IsStarted());

    assert(events.size() == 2);
    assert(events[0] == 11);
    assert(events[1] == 21);

    assert(bus.IsDeviceAttached(MemoryMap::RegionId::EeMmio));
    assert(bus.IsDeviceAttached(
        MemoryMap::RegionId::GsPrivilegedRegisters));

    assert(
        registry.GetDeviceState(1) ==
        DeviceRegistry::DeviceState::Running);

    assert(
        registry.ResetAll().status ==
        DeviceRegistry::DeviceStatus::Ok);

    assert(events.size() == 4);
    assert(events[2] == 12);
    assert(events[3] == 22);

    assert(
        registry.Stop().status ==
        DeviceRegistry::DeviceStatus::Ok);

    assert(!registry.IsStarted());
    assert(!registry.IsFaulted());

    assert(!bus.IsDeviceAttached(MemoryMap::RegionId::EeMmio));
    assert(!bus.IsDeviceAttached(
        MemoryMap::RegionId::GsPrivilegedRegisters));

    // Shutdown must occur in reverse dependency order.
    assert(events.size() == 6);
    assert(events[4] == 23);
    assert(events[5] == 13);

    assert(
        registry.GetDeviceState(1) ==
        DeviceRegistry::DeviceState::Stopped);

    memory.Shutdown();
}

void TestDuplicateRegionRejected()
{
    Memory::EE_Memory memory;
    Memory::Configuration configuration;

    assert(memory.Initialize(configuration) == Memory::Status::Ok);

    Bus::EE_MemoryBus bus(memory);
    DeviceRegistry::EE_DeviceRegistry registry(bus);

    std::vector<int> events;

    TestDevice first(1, events);
    TestDevice second(2, events);

    DeviceRegistry::DeviceRegistration registrationA =
        MakeRegistration(
            1,
            "First",
            &first,
            MemoryMap::RegionId::EeMmio);

    DeviceRegistry::DeviceRegistration registrationB =
        MakeRegistration(
            2,
            "Second",
            &second,
            MemoryMap::RegionId::EeMmio);

    assert(
        registry.RegisterDevice(registrationA).status ==
        DeviceRegistry::DeviceStatus::Ok);

    assert(
        registry.RegisterDevice(registrationB).status ==
        DeviceRegistry::DeviceStatus::RegionAlreadyRegistered);

    assert(registry.GetDeviceCount() == 1);

    memory.Shutdown();
}

void TestMissingDependencyRejectedAtStart()
{
    Memory::EE_Memory memory;
    Memory::Configuration configuration;

    assert(memory.Initialize(configuration) == Memory::Status::Ok);

    Bus::EE_MemoryBus bus(memory);
    DeviceRegistry::EE_DeviceRegistry registry(bus);

    std::vector<int> events;
    TestDevice device(1, events);

    DeviceRegistry::DeviceRegistration registration =
        MakeRegistration(
            1,
            "MissingDependencyDevice",
            &device,
            MemoryMap::RegionId::EeMmio);

    registration.dependencies.push_back(999);

    assert(
        registry.RegisterDevice(registration).status ==
        DeviceRegistry::DeviceStatus::Ok);

    assert(
        registry.Start().status ==
        DeviceRegistry::DeviceStatus::MissingDependency);

    assert(events.empty());
    assert(!registry.IsStarted());

    memory.Shutdown();
}

void TestDependencyCycleRejected()
{
    Memory::EE_Memory memory;
    Memory::Configuration configuration;

    assert(memory.Initialize(configuration) == Memory::Status::Ok);

    Bus::EE_MemoryBus bus(memory);
    DeviceRegistry::EE_DeviceRegistry registry(bus);

    std::vector<int> events;

    TestDevice first(1, events);
    TestDevice second(2, events);

    DeviceRegistry::DeviceRegistration registrationA =
        MakeRegistration(
            1,
            "CycleA",
            &first,
            MemoryMap::RegionId::EeMmio);

    DeviceRegistry::DeviceRegistration registrationB =
        MakeRegistration(
            2,
            "CycleB",
            &second,
            MemoryMap::RegionId::GsPrivilegedRegisters);

    registrationA.dependencies.push_back(2);
    registrationB.dependencies.push_back(1);

    assert(
        registry.RegisterDevice(registrationA).status ==
        DeviceRegistry::DeviceStatus::Ok);

    assert(
        registry.RegisterDevice(registrationB).status ==
        DeviceRegistry::DeviceStatus::Ok);

    assert(
        registry.Start().status ==
        DeviceRegistry::DeviceStatus::DependencyCycle);

    assert(events.empty());
    assert(!registry.IsStarted());

    memory.Shutdown();
}

void TestStartupRollback()
{
    Memory::EE_Memory memory;
    Memory::Configuration configuration;

    assert(memory.Initialize(configuration) == Memory::Status::Ok);

    Bus::EE_MemoryBus bus(memory);
    DeviceRegistry::EE_DeviceRegistry registry(bus);

    std::vector<int> events;

    TestDevice first(1, events);
    TestDevice second(2, events, true);

    DeviceRegistry::DeviceRegistration registrationA =
        MakeRegistration(
            1,
            "First",
            &first,
            MemoryMap::RegionId::EeMmio);

    DeviceRegistry::DeviceRegistration registrationB =
        MakeRegistration(
            2,
            "Second",
            &second,
            MemoryMap::RegionId::GsPrivilegedRegisters);

    registrationB.dependencies.push_back(1);

    assert(
        registry.RegisterDevice(registrationA).status ==
        DeviceRegistry::DeviceStatus::Ok);

    assert(
        registry.RegisterDevice(registrationB).status ==
        DeviceRegistry::DeviceStatus::Ok);

    assert(
        registry.Start().status ==
        DeviceRegistry::DeviceStatus::InitializationFailed);

    assert(!registry.IsStarted());
    assert(!registry.IsFaulted());

    // First initializes, second fails, then first is shut down.
    assert(events.size() == 3);
    assert(events[0] == 11);
    assert(events[1] == 21);
    assert(events[2] == 13);

    assert(!bus.IsDeviceAttached(MemoryMap::RegionId::EeMmio));
    assert(!bus.IsDeviceAttached(
        MemoryMap::RegionId::GsPrivilegedRegisters));

    memory.Shutdown();
}

} // namespace

int main()
{
    TestDependencyOrderAndLifecycle();
    TestDuplicateRegionRejected();
    TestMissingDependencyRejectedAtStart();
    TestDependencyCycleRejected();
    TestStartupRollback();

    return 0;
}
