#pragma once
#include <string>

class IServer {
public:
    virtual ~IServer() = default;

    virtual void Run() = 0;

    virtual void Stop() = 0;

    virtual void BroadcastMessage(const std::string& message) = 0;
};
