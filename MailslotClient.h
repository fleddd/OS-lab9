#pragma once
#include "IClient.h"
#include <windows.h>
#undef SendMessage
#include <string>
#include <thread>
#include <atomic>
#include <mutex>

class MailslotClient : public IClient {
public:
    MailslotClient();
    ~MailslotClient() override;

    void Run() override;
    void Stop() override;
    void SendMessage(const std::wstring& message) override;

private:
    std::atomic<bool> m_running;
    HANDLE m_clientMailslot;
    HANDLE m_serverWriteHandle;
    std::wstring m_userName;
    std::wstring m_serverId;
    std::wstring m_currentInput;
    std::mutex m_consoleMutex;
    
    void ListenServer();
    void ListenConsole();
    bool ConnectToServer();
    void ReprintInputLine();
};
