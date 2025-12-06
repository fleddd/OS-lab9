#pragma once
#include "IServer.h"
#include <string>

class PipeServer : public IServer {
public:
    PipeServer();
    ~PipeServer() override;

    void Run() override;
    void Stop() override;
    void BroadcastMessage(const std::string& message) override;

private:
    bool m_running;
    void ListenClients();
};
