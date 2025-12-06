#pragma once
#include "IClient.h"
#include <windows.h>
#undef SendMessage
#include <string>
#include <thread>
#include <atomic>

class PipeClient : public IClient {
public:
    PipeClient();
    ~PipeClient() override;
    void Run() override;
    void Stop() override;
    void SendMessage(const std::wstring& message);

private:
    std::atomic<bool> m_running;
    HANDLE m_readPipe;
    HANDLE m_writePipe;
    std::wstring m_userName;

    void ListenServer();
    void SenderLoop();
    bool ConnectToServer(const std::wstring& basePipeName);
};