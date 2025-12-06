#include "PipeServer.h"
#include <iostream>

PipeServer::PipeServer() : m_running(false) {}

PipeServer::~PipeServer() {
    Stop();
}

void PipeServer::Run() {
    m_running = true;
    std::cout << "PipeServer running...\n";
    ListenClients();
}

void PipeServer::Stop() {
    m_running = false;
    std::cout << "PipeServer stopped.\n";
}

void PipeServer::BroadcastMessage(const std::string& message) {
    // TODO: Реалізувати відправку всім клієнтам
    std::cout << "[PipeServer] Broadcasting: " << message << "\n";
}

void PipeServer::ListenClients() {
    // TODO: Реалізувати прийом повідомлень від клієнтів через Named Pipes
    while (m_running) {
        // Прийом і ретрансляція
    }
}
