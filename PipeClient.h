#pragma once
#include "IClient.h"
#include <windows.h>
#undef SendMessage
#include <string>
#include <thread>
#include <atomic>
#include <mutex>

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
std::wstring m_currentInput;
std::mutex m_consoleMutex;

void ListenServer();
void ListenConsole();
bool ConnectToServer(const std::wstring& basePipeName);
};