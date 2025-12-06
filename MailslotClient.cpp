#include "MailslotClient.h"
#include <iostream>

MailslotClient::MailslotClient() : m_running(false) {}
MailslotClient::~MailslotClient() {
    Stop();
}

void MailslotClient::Run() {
    m_running = true;
    std::cout << "MailslotClient running...\n";
    ListenServer();
}

void MailslotClient::Stop() {
    m_running = false;
    std::cout << "MailslotClient stopped.\n";
}

void MailslotClient::SendMessage(const std::string& message) {
    // TODO: Відправка повідомлення серверу через Mailslots
    std::cout << "[MailslotClient] Sending: " << message << "\n";
}

void MailslotClient::ListenServer() {
    // TODO: Прийом повідомлень від сервера
    while (m_running) {
        // Читання і відображення повідомлень
    }
}
