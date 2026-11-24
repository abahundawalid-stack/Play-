#include "../EE_DeviceRegistry.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace ORBIT2SX::EE;

namespace
{

int g_failures = 0;

#define CHECK(expression)                                      \
    do                                                         \
    {                                                          \
        if(!(expression))                                      \
        {                                                      \
            std::cerr << "FAILED: " << #expression             \
                      << " at " << __FILE__                    \
                      << ":" << __LINE__ << std::endl;         \
            ++g_failures;                                      \
        }                                                      \
    } while(false)

struct Event
{
    int device;
    char operation;
};

class TestDevice final : public DeviceRegistry::IEEDevice
{
public:
    TestDevice(
        int deviceId,
        std::vector<Event>& events)
        : m_deviceId(deviceId),
          m_events(events),
          m_failInitialize(false),
          m_failReset(false),
          m_failShutdown(false),
          m_initialized(false)
    {
    }

    void SetInitializeFailure(bool value)
    {
        m_failInitialize = value;
    }

    void SetResetFailure(bool value)
    {
        m_failReset = value;
    }

    void SetShutdownFailure(bool value)
    {
        m_failShutdown = value;
    }

    Bus::DeviceStatus Initialize() override
    {
        m_events.push_back(Event{m_deviceId, 'I'});

        if(m_failInitialize)
        {
            // Simulate partial resource allocation.
            m_initialized = true;
            return Bus::DeviceStatus::InternalError;
        }

        m_initialized = true;
        return Bus::DeviceStatus::Ok;
    }

    Bus::DeviceStatus Reset() override
    {
        m_events.push_back(Event{m_deviceId, 'R'});

        if(!m_initialized || m_failReset)
        {
            return Bus::DeviceStatus::AccessFault;
        }

        return Bus::DeviceStatus::Ok;
    }

    Bus::DeviceStatus Shutdown() override
    {
        m_events.push_back(Event{m_deviceId, 'S'});

        if(m_failShutdown)
        {
            return Bus::DeviceStatus::InternalError;
        }

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
    std::vector<Event>& m_events;

    bool m_failInitialize;
    bool m_failReset;
    bool m_failShutdown;
    bool m_initialized;
};

DeviceRegistry::DeviceRegistration MakeRegistration(
    DeviceRegistry::DeviceId id,
    const std::string& name,
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

bool IsEvent(
    const std::vector<Event>& events,
    std::size_t index,
    int device,
    char operation)
{
    return index < events.size() &&
           events[index].device == device &&
           events[index].operation == operation;
}

// ------------------------------------------------------------
// TEST: REGISTRATION VALIDATION
// ------------------------------------------------------------

void TestRegistrationValidation()
{
    Memory::EE_Memory memory;
    Memory::Configuration configuration;

    CHECK(memory.Initialize(configuration) == Memory::Status::Ok);

    Bus::EE_MemoryBus bus(memory);
    DeviceRegistry::EE_DeviceRegistry registry(bus);

    std::vector<Event> events;
    TestDevice device(1, events);

    DeviceRegistry::DeviceRegistration invalid;

    invalid.id = 0;
    invalid.name = "InvalidId";
    invalid.device = &device;

    CHECK(
        registry.RegisterDevice(invalid).status ==
        DeviceRegistry::DeviceStatus::InvalidArgument);

    invalid.id = 1;
    invalid.name.clear();

    CHECK(
        registry.RegisterDevice(invalid).status ==
        DeviceRegistry::DeviceStatus::InvalidArgument);

    invalid.name = "NullDevice";
    invalid.device = nullptr;

    CHECK(
        registry.RegisterDevice(invalid).status ==
        DeviceRegistry::DeviceStatus::InvalidArgument);

    DeviceRegistry::DeviceRegistration valid =
        MakeRegistration(
            1,
            "ValidDevice",
            &device,
            MemoryMap::RegionId::EeMmio);

    CHECK(
        registry.RegisterDevice(valid).status ==
        DeviceRegistry::DeviceStatus::Ok);

    CHECK(
        registry.RegisterDevice(valid).status ==
        DeviceRegistry::DeviceStatus::DuplicateDeviceId);

    CHECK(registry.GetDeviceCount() == 1);

    memory.Shutdown();
}

// ------------------------------------------------------------
// TEST: FORWARD DEPENDENCY AND STARTUP/SHUTDOWN ORDER
// ------------------------------------------------------------

void TestDependencyOrderAndLifecycle()
{
    Memory::EE_Memory memory;
    Memory::Configuration configuration;

    CHECK(memory.Initialize(configuration) == Memory::Status::Ok);

    Bus::EE_MemoryBus bus(memory);
    DeviceRegistry::EE_DeviceRegistry registry(bus);

    std::vector<Event> events;

    TestDevice base(1, events);
    TestDevice dependent(2, events);

    DeviceRegistry::DeviceRegistration dependentRegistration =
        MakeRegistration(
            2,
            "Dependent",
            &dependent,
            MemoryMap::RegionId::GsPrivilegedRegisters);

    dependentRegistration.dependencies.push_back(1);

    CHECK(
        registry.RegisterDevice(dependentRegistration).status ==
        DeviceRegistry::DeviceStatus::Ok);

    DeviceRegistry::DeviceRegistration baseRegistration =
        MakeRegistration(
            1,
            "Base",
            &base,
            MemoryMap::RegionId::EeMmio);

    CHECK(
        registry.RegisterDevice(baseRegistration).status ==
        DeviceRegistry::DeviceStatus::Ok);

    CHECK(registry.GetDeviceCount() == 2);
    CHECK(registry.ContainsDevice(1));
    CHECK(registry.ContainsDevice(2));

    CHECK(
        registry.Start().status ==
        DeviceRegistry::DeviceStatus::Ok);

    CHECK(registry.IsStarted());
    CHECK(!registry.IsFaulted());

    CHECK(IsEvent(events, 0, 1, 'I'));
    CHECK(IsEvent(events, 1, 2, 'I'));

    CHECK(
        registry.GetDeviceState(1) ==
        DeviceRegistry::DeviceState::Running);

    CHECK(
        registry.GetDeviceState(2) ==
        DeviceRegistry::DeviceState::Running);

    CHECK(bus.IsDeviceAttached(MemoryMap::RegionId::EeMmio));

    CHECK(
        bus.IsDeviceAttached(
            MemoryMap::RegionId::GsPrivilegedRegisters));

    CHECK(
        registry.ResetAll().status ==
        DeviceRegistry::DeviceStatus::Ok);

    CHECK(IsEvent(events, 2, 1, 'R'));
    CHECK(IsEvent(events, 3, 2, 'R'));

    CHECK(
        registry.Stop().status ==
        DeviceRegistry::DeviceStatus::Ok);

    CHECK(!registry.IsStarted());
    CHECK(!registry.IsFaulted());

    CHECK(!bus.IsDeviceAttached(MemoryMap::RegionId::EeMmio));

    CHECK(
        !bus.IsDeviceAttached(
            MemoryMap::RegionId::GsPrivilegedRegisters));

    CHECK(IsEvent(events, 4, 2, 'S'));
    CHECK(IsEvent(events, 5, 1, 'S'));

    CHECK(
        registry.GetDeviceState(1) ==
        DeviceRegistry::DeviceState::Stopped);

    memory.Shutdown();
}

// ------------------------------------------------------------
// TEST: ONE DEVICE, MULTIPLE REGIONS
// ------------------------------------------------------------

void TestMultipleRegionsPerDevice()
{
    Memory::EE_Memory memory;
    Memory::Configuration configuration;

    CHECK(memory.Initialize(configuration) == Memory::Status::Ok);

    Bus::EE_MemoryBus bus(memory);
    DeviceRegistry::EE_DeviceRegistry registry(bus);

    std::vector<Event> events;
    TestDevice vu0(10, events);

    DeviceRegistry::DeviceRegistration registration;

    registration.id = 10;
    registration.name = "VU0";
    registration.device = &vu0;

    registration.regions.push_back(
        MemoryMap::RegionId::Vu0MicroMemory);

    registration.regions.push_back(
        MemoryMap::RegionId::Vu0DataMemory);

    CHECK(
        registry.RegisterDevice(registration).status ==
        DeviceRegistry::DeviceStatus::Ok);

    CHECK(
        registry.Start().status ==
        DeviceRegistry::DeviceStatus::Ok);

    CHECK(
        bus.IsDeviceAttached(
            MemoryMap::RegionId::Vu0MicroMemory));

    CHECK(
        bus.IsDeviceAttached(
            MemoryMap::RegionId::Vu0DataMemory));

    CHECK(
        registry.Stop().status ==
        DeviceRegistry::DeviceStatus::Ok);

    CHECK(
        !bus.IsDeviceAttached(
            MemoryMap::RegionId::Vu0MicroMemory));

    CHECK(
        !bus.IsDeviceAttached(
            MemoryMap::RegionId::Vu0DataMemory));

    memory.Shutdown();
}

// ------------------------------------------------------------
// TEST: REGION OWNERSHIP CONFLICT
// ------------------------------------------------------------

void TestDuplicateRegionRejected()
{
    Memory::EE_Memory memory;
    Memory::Configuration configuration;

    CHECK(memory.Initialize(configuration) == Memory::Status::Ok);

    Bus::EE_MemoryBus bus(memory);
    DeviceRegistry::EE_DeviceRegistry registry(bus);

    std::vector<Event> events;

    TestDevice first(1, events);
    TestDevice second(2, events);

    DeviceRegistry::DeviceRegistration firstRegistration =
        MakeRegistration(
            1,
            "First",
            &first,
            MemoryMap::RegionId::EeMmio);

    DeviceRegistry::DeviceRegistration secondRegistration =
        MakeRegistration(
            2,
            "Second",
            &second,
            MemoryMap::RegionId::EeMmio);

    CHECK(
        registry.RegisterDevice(firstRegistration).status ==
        DeviceRegistry::DeviceStatus::Ok);

    CHECK(
        registry.RegisterDevice(secondRegistration).status ==
        DeviceRegistry::DeviceStatus::RegionAlreadyRegistered);

    CHECK(registry.GetDeviceCount() == 1);

    memory.Shutdown();
}

// ------------------------------------------------------------
// TEST: DEPENDENCY VALIDATION
// ------------------------------------------------------------

void TestMissingDependencyRejectedAtStart()
{
    Memory::EE_Memory memory;
    Memory::Configuration configuration;

    CHECK(memory.Initialize(configuration) == Memory::Status::Ok);

    Bus::EE_MemoryBus bus(memory);
    DeviceRegistry::EE_DeviceRegistry registry(bus);

    std::vector<Event> events;
    TestDevice device(1, events);

    DeviceRegistry::DeviceRegistration registration =
        MakeRegistration(
            1,
            "MissingDependency",
            &device,
            MemoryMap::RegionId::EeMmio);

    registration.dependencies.push_back(999);

    CHECK(
        registry.RegisterDevice(registration).status ==
        DeviceRegistry::DeviceStatus::Ok);

    CHECK(
        registry.Start().status ==
        DeviceRegistry::DeviceStatus::MissingDependency);

    CHECK(events.empty());
    CHECK(!registry.IsStarted());

    memory.Shutdown();
}

void TestDependencyCycleRejected()
{
    Memory::EE_Memory memory;
    Memory::Configuration configuration;

    CHECK(memory.Initialize(configuration) == Memory::Status::Ok);

    Bus::EE_MemoryBus bus(memory);
    DeviceRegistry::EE_DeviceRegistry registry(bus);

    std::vector<Event> events;

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

    CHECK(
        registry.RegisterDevice(registrationA).status ==
        DeviceRegistry::DeviceStatus::Ok);

    CHECK(
        registry.RegisterDevice(registrationB).status ==
        DeviceRegistry::DeviceStatus::Ok);

    CHECK(
        registry.Start().status ==
        DeviceRegistry::DeviceStatus::DependencyCycle);

    CHECK(events.empty());
    CHECK(!registry.IsStarted());

    memory.Shutdown();
}

void TestUnregisterDependencyInUse()
{
    Memory::EE_Memory memory;
    Memory::Configuration configuration;

    CHECK(memory.Initialize(configuration) == Memory::Status::Ok);

    Bus::EE_MemoryBus bus(memory);
    DeviceRegistry::EE_DeviceRegistry registry(bus);

    std::vector<Event> events;

    TestDevice base(1, events);
    TestDevice dependent(2, events);

    DeviceRegistry::DeviceRegistration baseRegistration =
        MakeRegistration(
            1,
            "Base",
            &base,
            MemoryMap::RegionId::EeMmio);

    DeviceRegistry::DeviceRegistration dependentRegistration =
        MakeRegistration(
            2,
            "Dependent",
            &dependent,
            MemoryMap::RegionId::GsPrivilegedRegisters);

    dependentRegistration.dependencies.push_back(1);

    CHECK(
        registry.RegisterDevice(baseRegistration).status ==
        DeviceRegistry::DeviceStatus::Ok);

    CHECK(
        registry.RegisterDevice(dependentRegistration).status ==
        DeviceRegistry::DeviceStatus::Ok);

    CHECK(
        registry.UnregisterDevice(1).status ==
        DeviceRegistry::DeviceStatus::DependencyInUse);

    CHECK(
        registry.UnregisterDevice(2).status ==
        DeviceRegistry::DeviceStatus::Ok);

    CHECK(
        registry.UnregisterDevice(1).status ==
        DeviceRegistry::DeviceStatus::Ok);

    memory.Shutdown();
}

// ------------------------------------------------------------
// TEST: PARTIAL INITIALIZATION ROLLBACK
// ------------------------------------------------------------

void TestStartupRollback()
{
    Memory::EE_Memory memory;
    Memory::Configuration configuration;

    CHECK(memory.Initialize(configuration) == Memory::Status::Ok);

    Bus::EE_MemoryBus bus(memory);
    DeviceRegistry::EE_DeviceRegistry registry(bus);

    std::vector<Event> events;

    TestDevice first(1, events);
    TestDevice second(2, events);

    second.SetInitializeFailure(true);

    DeviceRegistry::DeviceRegistration firstRegistration =
        MakeRegistration(
            1,
            "First",
            &first,
            MemoryMap::RegionId::EeMmio);

    DeviceRegistry::DeviceRegistration secondRegistration =
        MakeRegistration(
            2,
            "Second",
            &second,
            MemoryMap::RegionId::GsPrivilegedRegisters);

    secondRegistration.dependencies.push_back(1);

    CHECK(
        registry.RegisterDevice(firstRegistration).status ==
        DeviceRegistry::DeviceStatus::Ok);

    CHECK(
        registry.RegisterDevice(secondRegistration).status ==
        DeviceRegistry::DeviceStatus::Ok);

    CHECK(
        registry.Start().status ==
        DeviceRegistry::DeviceStatus::InitializationFailed);

    CHECK(!registry.IsStarted());
    CHECK(!registry.IsFaulted());

    // First initialized.
    CHECK(IsEvent(events, 0, 1, 'I'));

    // Second initialization failed after partial allocation.
    CHECK(IsEvent(events, 1, 2, 'I'));

    // The failing device is cleaned first.
    CHECK(IsEvent(events, 2, 2, 'S'));

    // Then the previously initialized dependency is cleaned.
    CHECK(IsEvent(events, 3, 1, 'S'));

    CHECK(
        !bus.IsDeviceAttached(
            MemoryMap::RegionId::EeMmio));

    CHECK(
        !bus.IsDeviceAttached(
            MemoryMap::RegionId::GsPrivilegedRegisters));

    memory.Shutdown();
}

// ------------------------------------------------------------
// TEST: SHUTDOWN FAILURE AND RETRY
// ------------------------------------------------------------

void TestShutdownFailureCanBeRetried()
{
    Memory::EE_Memory memory;
    Memory::Configuration configuration;

    CHECK(memory.Initialize(configuration) == Memory::Status::Ok);

    Bus::EE_MemoryBus bus(memory);
    DeviceRegistry::EE_DeviceRegistry registry(bus);

    std::vector<Event> events;
    TestDevice device(1, events);

    DeviceRegistry::DeviceRegistration registration =
        MakeRegistration(
            1,
            "RetryDevice",
            &device,
            MemoryMap::RegionId::EeMmio);

    CHECK(
        registry.RegisterDevice(registration).status ==
        DeviceRegistry::DeviceStatus::Ok);

    CHECK(
        registry.Start().status ==
        DeviceRegistry::DeviceStatus::Ok);

    device.SetShutdownFailure(true);

    CHECK(
        registry.Stop().status ==
        DeviceRegistry::DeviceStatus::ShutdownFailed);

    CHECK(!registry.IsStarted());
    CHECK(registry.IsFaulted());

    // Endpoint must already be detached even though Shutdown failed.
    CHECK(
        !bus.IsDeviceAttached(
            MemoryMap::RegionId::EeMmio));

    device.SetShutdownFailure(false);

    CHECK(
        registry.Stop().status ==
        DeviceRegistry::DeviceStatus::Ok);

    CHECK(!registry.IsFaulted());

    CHECK(
        registry.GetDeviceState(1) ==
        DeviceRegistry::DeviceState::Stopped);

    memory.Shutdown();
}

// ------------------------------------------------------------
// TEST: RESET FAILURE REQUIRES STOP
// ------------------------------------------------------------

void TestResetFailureTransitionsRegistryToFaulted()
{
    Memory::EE_Memory memory;
    Memory::Configuration configuration;

    CHECK(memory.Initialize(configuration) == Memory::Status::Ok);

    Bus::EE_MemoryBus bus(memory);
    DeviceRegistry::EE_DeviceRegistry registry(bus);

    std::vector<Event> events;
    TestDevice device(1, events);

    DeviceRegistry::DeviceRegistration registration =
        MakeRegistration(
            1,
            "ResetFailureDevice",
            &device,
            MemoryMap::RegionId::EeMmio);

    CHECK(
        registry.RegisterDevice(registration).status ==
        DeviceRegistry::DeviceStatus::Ok);

    CHECK(
        registry.Start().status ==
        DeviceRegistry::DeviceStatus::Ok);

    device.SetResetFailure(true);

    CHECK(
        registry.ResetAll().status ==
        DeviceRegistry::DeviceStatus::ResetFailed);

    CHECK(registry.IsStarted());
    CHECK(registry.IsFaulted());

    CHECK(
        registry.Start().status ==
        DeviceRegistry::DeviceStatus::RegistryFaulted);

    CHECK(
        registry.Stop().status ==
        DeviceRegistry::DeviceStatus::Ok);

    CHECK(!registry.IsStarted());
    CHECK(!registry.IsFaulted());

    memory.Shutdown();
}

// ------------------------------------------------------------
// TEST: RESTART AFTER CLEAN STOP
// ------------------------------------------------------------

void TestRestartAfterStop()
{
    Memory::EE_Memory memory;
    Memory::Configuration configuration;

    CHECK(memory.Initialize(configuration) == Memory::Status::Ok);

    Bus::EE_MemoryBus bus(memory);
    DeviceRegistry::EE_DeviceRegistry registry(bus);

    std::vector<Event> events;
    TestDevice device(1, events);

    DeviceRegistry::DeviceRegistration registration =
        MakeRegistration(
            1,
            "RestartableDevice",
            &device,
            MemoryMap::RegionId::EeMmio);

    CHECK(
        registry.RegisterDevice(registration).status ==
        DeviceRegistry::DeviceStatus::Ok);

    CHECK(
        registry.Start().status ==
        DeviceRegistry::DeviceStatus::Ok);

    CHECK(
        registry.Stop().status ==
        DeviceRegistry::DeviceStatus::Ok);

    CHECK(
        registry.Start().status ==
        DeviceRegistry::DeviceStatus::Ok);

    CHECK(registry.IsStarted());

    CHECK(
        registry.GetDeviceState(1) ==
        DeviceRegistry::DeviceState::Running);

    CHECK(
        registry.Stop().status ==
        DeviceRegistry::DeviceStatus::Ok);

    memory.Shutdown();
}

} // namespace

int main()
{
    TestRegistrationValidation();
    TestDependencyOrderAndLifecycle();
    TestMultipleRegionsPerDevice();
    TestDuplicateRegionRejected();

    TestMissingDependencyRejectedAtStart();
    TestDependencyCycleRejected();
    TestUnregisterDependencyInUse();

    TestStartupRollback();
    TestShutdownFailureCanBeRetried();
    TestResetFailureTransitionsRegistryToFaulted();
    TestRestartAfterStop();

    if(g_failures != 0)
    {
        std::cerr
            << "EE_DeviceRegistry tests failed: "
            << g_failures
            << std::endl;

        return EXIT_FAILURE;
    }

    std::cout
        << "All EE_DeviceRegistry tests passed."
        << std::endl;

    return EXIT_SUCCESS;
}
