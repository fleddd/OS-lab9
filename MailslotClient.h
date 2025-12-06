#pragma once
#include "IClient.h"

#include <string>
#undef SendMessage;

class MailslotClient : public IClient {
public:
    MailslotClient();
    ~MailslotClient() override;

    void Run() override;
    void Stop() override;
    void SendMessage(const std::wstring& message) override;


private:
    bool m_running;
    void ListenServer();
};
