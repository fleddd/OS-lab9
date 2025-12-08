#pragma once
#include "IServer.h"
#include <windows.h>
#include <string>
#include <thread>
#include <atomic>
#include <vector>
#include <mutex>

class MailslotServer : public IServer {
public:
    MailslotServer();
    ~MailslotServer() override;

    void Run() override;
    void Stop() override;
    void BroadcastMessage(const std::wstring& senderId, const std::wstring& message) override;

private:
    std::atomic<bool> m_running;
    HANDLE m_serverMailslot;
    std::wstring m_serverId;
    std::vector<std::wstring> m_clients;
    std::mutex m_clientsMutex;
    
    void ListenClients();
    void RemoveClient(const std::wstring& clientMailslot);
};
