#pragma once
#include <string>

class IClient {
public:
    virtual ~IClient() = default;

    virtual void Run() = 0;
    virtual void Stop() = 0;
    virtual void SendMessage(const std::wstring& message) = 0;
};
