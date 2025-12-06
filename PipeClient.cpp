#include "PipeClient.h"
#include <iostream>

PipeClient::PipeClient() : m_running(false) {}
PipeClient::~PipeClient() {
    Stop();
}

void PipeClient::Run() {
    m_running = true;
    std::cout << "PipeClient running...\n";
    ListenServer();
}

void PipeClient::Stop() {
    m_running = false;
    std::cout << "PipeClient stopped.\n";
}

void PipeClient::SendMessage(const std::string& message) {
    // TODO: Реалізувати відправку повідомлення серверу через Named Pipes
    std::cout << "[PipeClient] Sending: " << message << "\n";
}

void PipeClient::ListenServer() {
    // TODO: Реалізувати прийом повідомлень від сервера
    while (m_running) {
        // Читання і відображення повідомлень
    }
}
