#pragma once
#include <string>

class IClient {
public:
    virtual ~IClient() = default;
    // Повинен блокувати потік та слухати сервер
    virtual void Run() = 0;

    // Метод зупинки клієнта
    virtual void Stop() = 0;

    // Надсилання повідомлення серверу
    virtual void SendMessage(const std::string& message) = 0;
};
