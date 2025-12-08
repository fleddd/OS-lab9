#include "MailslotServer.h"
#include "Config.h"
#include <iostream>
#include <algorithm>

using namespace std;

MailslotServer::MailslotServer() : m_running(false), m_serverMailslot(INVALID_HANDLE_VALUE) {}

MailslotServer::~MailslotServer() {
    Stop();
}

void MailslotServer::Run() {
    m_running = true;
    m_serverId = to_wstring(GetCurrentProcessId());

    wstring mailslotName = wstring(MAILSLOT_BASE_NAME) + m_serverId;
    wcout << L"Server ID: " << m_serverId << endl;
    wcout << L"Server started on mailslot: " << mailslotName << endl;

    thread(&MailslotServer::ListenClients, this).detach();

    while (m_running) {
        Sleep(100);
    }
}

void MailslotServer::Stop() {
    m_running = false;
    
    if (m_serverMailslot != INVALID_HANDLE_VALUE) {
        CloseHandle(m_serverMailslot);
        m_serverMailslot = INVALID_HANDLE_VALUE;
    }

    lock_guard<mutex> lock(m_clientsMutex);
    m_clients.clear();
}

void MailslotServer::BroadcastMessage(const wstring& sender, const wstring& message) {
    wstring fullMsg = L"[" + sender + L"]: " + message;

    vector<wstring> clientsCopy;
    {
        lock_guard<mutex> lock(m_clientsMutex);
        clientsCopy = m_clients;
    }

    for (auto& client : clientsCopy) {
        HANDLE hClientMailslot = CreateFile(
            client.c_str(),
            GENERIC_WRITE,
            FILE_SHARE_READ,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );

        if (hClientMailslot == INVALID_HANDLE_VALUE) {
            DWORD err = GetLastError();
            if (err == ERROR_FILE_NOT_FOUND) {
                RemoveClient(client);
            }
            else {
                wcout << L"Failed to send to " << client << L" (error: " << err << L")" << endl;
            }
            continue;
        }

        DWORD written = 0;
        DWORD msgSize = (DWORD)((fullMsg.size() + 1) * sizeof(wchar_t));

        BOOL ok = WriteFile(hClientMailslot, fullMsg.c_str(), msgSize, &written, nullptr);

        if (!ok || written == 0) {
            DWORD err = GetLastError();
            wcout << L"Failed to send to " << client << L" (error: " << err << L")" << endl;
        }

        CloseHandle(hClientMailslot);
    }
}

void MailslotServer::RemoveClient(const wstring& clientMailslot) {
    lock_guard<mutex> lock(m_clientsMutex);

    auto it = find_if(m_clients.begin(), m_clients.end(),
        [&clientMailslot](const wstring& c) { return c == clientMailslot; });

    if (it != m_clients.end()) {
        wcout << L"Client " << *it << L" disconnected" << endl;
        m_clients.erase(it);
    }
}

void MailslotServer::ListenClients() {
    wstring mailslotName = wstring(MAILSLOT_BASE_NAME) + m_serverId;

    // Створюємо mailslot для прийому повідомлень від клієнтів
    m_serverMailslot = CreateMailslot(
        mailslotName.c_str(),
        0,
        MAILSLOT_WAIT_FOREVER,
        nullptr
    );

    if (m_serverMailslot == INVALID_HANDLE_VALUE) {
        cerr << "Failed to create server mailslot. Error: " << GetLastError() << endl;
        return;
    }

    wchar_t buffer[MAILSLOT_BUFFER_SIZE];

    while (m_running) {
        DWORD bytesRead = 0;
        DWORD nextSize = 0;
        DWORD messageCount = 0;

        BOOL ok = GetMailslotInfo(
            m_serverMailslot,
            nullptr,
            &nextSize,
            &messageCount,
            nullptr
        );

        if (!ok) {
            DWORD err = GetLastError();
            cerr << "GetMailslotInfo failed. Error: " << err << endl;
            Sleep(100);
            continue;
        }

        if (messageCount == 0 || nextSize == MAILSLOT_NO_MESSAGE) {
            Sleep(100);
            continue;
        }

        ok = ReadFile(m_serverMailslot, buffer, sizeof(buffer), &bytesRead, nullptr);

        if (!ok || bytesRead == 0) {
            DWORD err = GetLastError();
            cerr << "Failed to read from mailslot. Error: " << err << endl;
            continue;
        }

        size_t charCount = bytesRead / sizeof(wchar_t);
        wstring msg(buffer, charCount);

        if (!msg.empty() && msg.back() == L'\0') {
            msg.pop_back();
        }

        // Парсимо повідомлення: формат "CLIENTMAILSLOT|USERNAME|MESSAGE"
        size_t firstPipe = msg.find(L'|');
        if (firstPipe == wstring::npos) continue;

        wstring clientMailslot = msg.substr(0, firstPipe);
        size_t secondPipe = msg.find(L'|', firstPipe + 1);
        if (secondPipe == wstring::npos) continue;

        wstring userName = msg.substr(firstPipe + 1, secondPipe - firstPipe - 1);
        wstring userMessage = msg.substr(secondPipe + 1);

        {
            lock_guard<mutex> lock(m_clientsMutex);
            auto it = find_if(m_clients.begin(), m_clients.end(),
                [&clientMailslot](const wstring& c) { return c == clientMailslot; });
            
            if (it == m_clients.end()) {
                m_clients.push_back(clientMailslot);
                wcout << L"Client " << clientMailslot << L" connected" << endl;
            }
        }

        if (!userMessage.empty()) {
            BroadcastMessage(userName, userMessage);
        }
    }
}
