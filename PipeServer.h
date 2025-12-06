#pragma once
#include "IServer.h"
#include <windows.h>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <map>

class PipeServer : public IServer {
public:
    PipeServer();
    ~PipeServer() override;
    void Run() override;
    void Stop() override;

    void BroadcastMessage(const std::wstring& sender, const std::wstring& message) override;

    void BroadcastMessage(const std::wstring& sender, const std::wstring& message, HANDLE excludePipe);

private:
    struct ClientInfo {
        HANDLE readPipe;
        HANDLE writePipe;
        std::wstring name;
    };

    std::atomic<bool> m_running;
    std::vector<ClientInfo> m_clients;
    std::mutex m_clientsMutex;

    void WaitForClients(const std::wstring& basePipeName);
    void HandleClient(HANDLE readPipe, HANDLE writePipe);
    void RemoveClient(HANDLE readPipe);
};