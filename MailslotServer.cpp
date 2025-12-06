#include "MailslotServer.h"
#include <iostream>

MailslotServer::MailslotServer() : m_running(false) {}
MailslotServer::~MailslotServer() {
    Stop();
}

void MailslotServer::Run() {
    m_running = true;
    std::cout << "MailslotServer running...\n";
    ListenClients();
}

void MailslotServer::Stop() {
    m_running = false;
    std::cout << "MailslotServer stopped.\n";
}

void MailslotServer::BroadcastMessage(const std::wstring& senderId, const std::wstring& message) {
    // TODO: Реалізувати Broadcast через Mailslots
}

void MailslotServer::ListenClients() {
    // TODO: Прийом повідомлень від клієнтів
    while (m_running) {
        // Читання і ретрансляція
    }
}
