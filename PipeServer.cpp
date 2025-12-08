#include "PipeServer.h"
#include "Config.h"
#include <iostream>
#include <algorithm>

using namespace std;

PipeServer::PipeServer() : m_running(false) {}

PipeServer::~PipeServer() {
    Stop();
}

void PipeServer::Run() {
    m_running = true;
    wstring id = to_wstring(GetCurrentProcessId());

    wstring basePipeName = wstring(PIPE_BASE_NAME) + id;
    wcout << L"Server ID: " << id << endl;
    wcout << L"Server started on pipes: " << basePipeName << L"_*" << endl;

    thread(&PipeServer::WaitForClients, this, basePipeName).detach();

    while (m_running) {
        Sleep(100);
    }
}

void PipeServer::Stop() {
    m_running = false;
    lock_guard<mutex> lock(m_clientsMutex);

    for (auto& client : m_clients) {
        DisconnectNamedPipe(client.readPipe);
        DisconnectNamedPipe(client.writePipe);
        CloseHandle(client.readPipe);
        CloseHandle(client.writePipe);
    }
    m_clients.clear();
}

void PipeServer::BroadcastMessage(const wstring& sender, const wstring& message) {
    BroadcastMessage(sender, message, nullptr);
}

void PipeServer::BroadcastMessage(const wstring& sender, const wstring& message, HANDLE excludePipe) {
    wstring fullMsg = L"[" + sender + L"]: " + message;

    vector<ClientInfo> clientsCopy;
    {
        lock_guard<mutex> lock(m_clientsMutex);
        clientsCopy = m_clients;
    }

    for (auto& client : clientsCopy) {
        DWORD written = 0;
        DWORD msgSize = (DWORD)((fullMsg.size() + 1) * sizeof(wchar_t));

        BOOL ok = WriteFile(client.writePipe, fullMsg.c_str(), msgSize, &written, nullptr);

        if (!ok || written == 0) {
            DWORD err = GetLastError();
            wcout << L"Failed to send to " << client.name << L" (error: " << err << L")" << endl;

            if (err == ERROR_BROKEN_PIPE || err == ERROR_NO_DATA) {
                RemoveClient(client.readPipe);
            }
        }
    }
}

void PipeServer::RemoveClient(HANDLE readPipe) {
    lock_guard<mutex> lock(m_clientsMutex);

    auto it = find_if(m_clients.begin(), m_clients.end(),
        [readPipe](const ClientInfo& c) { return c.readPipe == readPipe; });

    if (it != m_clients.end()) {
        wcout << L"Client " << it->name << L" disconnected." << endl;
        CloseHandle(it->readPipe);
        CloseHandle(it->writePipe);
        m_clients.erase(it);
    }
}

void PipeServer::WaitForClients(const wstring& basePipeName) {
    while (m_running) {
        wstring readPipeName = basePipeName + L"_read";
        wstring writePipeName = basePipeName + L"_write";

        // Створюємо pipe для читання (сервер читає від клієнта)
        HANDLE hReadPipe = CreateNamedPipe(
            readPipeName.c_str(),
            PIPE_ACCESS_INBOUND,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            PIPE_BUFFER_SIZE,
            PIPE_BUFFER_SIZE,
            0,
            nullptr
        );

        if (hReadPipe == INVALID_HANDLE_VALUE) {
            cerr << "Failed to create read pipe. Error: " << GetLastError() << endl;
            Sleep(1000);
            continue;
        }

        // Створюємо pipe для запису (сервер пише до клієнта)
        HANDLE hWritePipe = CreateNamedPipe(
            writePipeName.c_str(),
            PIPE_ACCESS_OUTBOUND,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            PIPE_BUFFER_SIZE,
            PIPE_BUFFER_SIZE,
            0,
            nullptr
        );

        if (hWritePipe == INVALID_HANDLE_VALUE) {
            cerr << "Failed to create write pipe. Error: " << GetLastError() << endl;
            CloseHandle(hReadPipe);
            Sleep(1000);
            continue;
        }

        wcout << L"Waiting for client..." << endl;

        // Чекаємо підключення до read pipe
        BOOL connected = ConnectNamedPipe(hReadPipe, nullptr);
        if (!connected && GetLastError() != ERROR_PIPE_CONNECTED) {
            CloseHandle(hReadPipe);
            CloseHandle(hWritePipe);
            continue;
        }

        // Чекаємо підключення до write pipe
        connected = ConnectNamedPipe(hWritePipe, nullptr);
        if (!connected && GetLastError() != ERROR_PIPE_CONNECTED) {
            DisconnectNamedPipe(hReadPipe);
            CloseHandle(hReadPipe);
            CloseHandle(hWritePipe);
            continue;
        }

        wcout << L"Client connected, waiting for name..." << endl;

        // Читаємо ім'я клієнта (перше повідомлення)
        wchar_t nameBuffer[256] = { 0 };
        DWORD bytesRead = 0;
        BOOL ok = ReadFile(hReadPipe, nameBuffer, sizeof(nameBuffer), &bytesRead, nullptr);

        if (!ok || bytesRead == 0) {
            wcout << L"Failed to read client name." << endl;
            DisconnectNamedPipe(hReadPipe);
            DisconnectNamedPipe(hWritePipe);
            CloseHandle(hReadPipe);
            CloseHandle(hWritePipe);
            continue;
        }

        wstring clientName(nameBuffer);
        wcout << L"Client '" << clientName << L"' joined the chat!" << endl;

        {
            lock_guard<mutex> lock(m_clientsMutex);
            m_clients.push_back({ hReadPipe, hWritePipe, clientName });
        }

        // Повідомляємо всіх про нового клієнта (КРІМ нього самого)
        BroadcastMessage(L"SERVER", clientName + L" joined the chat", hReadPipe);

        thread(&PipeServer::HandleClient, this, hReadPipe, hWritePipe).detach();
    }
}

void PipeServer::HandleClient(HANDLE readPipe, HANDLE writePipe) {
    wchar_t buffer[PIPE_BUFFER_SIZE];

    // Знаходимо ім'я клієнта
    wstring clientName;
    {
        lock_guard<mutex> lock(m_clientsMutex);
        auto it = find_if(m_clients.begin(), m_clients.end(),
            [readPipe](const ClientInfo& c) { return c.readPipe == readPipe; });
        if (it != m_clients.end()) {
            clientName = it->name;
        }
    }

    while (m_running) {
        DWORD bytesRead = 0;
        BOOL ok = ReadFile(readPipe, buffer, sizeof(buffer), &bytesRead, nullptr);

        if (!ok || bytesRead == 0) {
            wcout << L"Client " << clientName << L" connection lost." << endl;
            break;
        }

        // Перетворюємо прочитані байти в wstring
        size_t charCount = bytesRead / sizeof(wchar_t);
        wstring msg(buffer, charCount);

        // Видаляємо null-термінатор якщо є
        if (!msg.empty() && msg.back() == L'\0') {
            msg.pop_back();
        }

        // НЕ виводимо повідомлення на сервері, тільки пересилаємо
        // Сервер працює як хаб - тільки перенаправляє повідомлення

        // Розсилаємо всім КРІМ відправника
        BroadcastMessage(clientName, msg, readPipe);
    }

    DisconnectNamedPipe(readPipe);
    DisconnectNamedPipe(writePipe);
    RemoveClient(readPipe);

    // Повідомляємо про від'єднання (того клієнта вже немає, тому excludePipe не потрібен)
    BroadcastMessage(L"SERVER", clientName + L" left the chat");
}