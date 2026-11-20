#include "MailBox.h"

bool CMailBox::IsPending() const
{
    std::lock_guard<std::mutex> lock(m_callMutex);
    return !m_calls.empty();
}

void CMailBox::WaitForCall()
{
    std::unique_lock<std::mutex> lock(m_callMutex);

    m_waitCondition.wait(lock, [this]() {
        return !m_calls.empty();
    });
}

void CMailBox::WaitForCall(unsigned int timeOut)
{
    std::unique_lock<std::mutex> lock(m_callMutex);

    m_waitCondition.wait_for(
        lock,
        std::chrono::milliseconds(timeOut),
        [this]() {
            return !m_calls.empty();
        });
}

void CMailBox::FlushCalls()
{
    SendCall([]() {}, true);
}

void CMailBox::SendCall(
    const FunctionType& function,
    bool waitForCompletion)
{
    std::future<void> future;

    {
        MESSAGE message;
        message.function = function;

        if(waitForCompletion)
        {
            message.promise =
                std::make_unique<std::promise<void>>();

            future = message.promise->get_future();
        }

        {
            std::lock_guard<std::mutex> lock(m_callMutex);
            m_calls.push_back(std::move(message));
        }
    }

    m_waitCondition.notify_all();

    if(waitForCompletion)
    {
        future.wait();
    }
}

void CMailBox::SendCall(FunctionType&& function)
{
    {
        MESSAGE message;
        message.function = std::move(function);

        std::lock_guard<std::mutex> lock(m_callMutex);
        m_calls.push_back(std::move(message));
    }

    m_waitCondition.notify_all();
}

void CMailBox::ReceiveCall()
{
    MESSAGE message;

    {
        std::lock_guard<std::mutex> lock(m_callMutex);

        if(m_calls.empty())
        {
            return;
        }

        message = std::move(m_calls.front());
        m_calls.pop_front();
    }

    message.function();

    if(message.promise)
    {
        message.promise->set_value();
    }
}
