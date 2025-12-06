#pragma once
#include "IServer.h"
#include <string>

class MailslotServer : public IServer {
public:
    MailslotServer();
    ~MailslotServer() override;

    void Run() override;
    void Stop() override;
    void BroadcastMessage(const std::string& message) override;

private:
    bool m_running;
    void ListenClients();
};
