#include "MailslotClient.h"
#include "Config.h"
#include <iostream>
#include <limits>
#include <sstream>
#include <mutex>
#include <conio.h>

using namespace std;

constexpr int INPUT_PROMPT_LENGTH = sizeof(INPUT_PROMPT) / sizeof(wchar_t) - 1;

MailslotClient::MailslotClient() : m_running(false), m_clientMailslot(INVALID_HANDLE_VALUE), m_serverWriteHandle(INVALID_HANDLE_VALUE) {}

MailslotClient::~MailslotClient() {
    Stop();
}

void MailslotClient::Run() {
    m_running = true;

    string id, name;
    cout << "Enter Server ID: ";
    cin >> id;
    cout << "Enter your name: ";
    cin >> name;

    m_userName = wstring(name.begin(), name.end());
    m_serverId = wstring(id.begin(), id.end());

    if (!ConnectToServer()) {
        cout << "Failed to connect to server" << endl;
        Stop();
        return;
    }

    // Повідомляємо сервер про нового клієнта
    SendMessage(L"");

    wcout << L"Connected to server as '" << m_userName << L"'" << endl;
    wcout << L"Start chatting" << endl;
    wcout << INPUT_PROMPT << flush;

    thread listener(&MailslotClient::ListenServer, this);
    thread sender(&MailslotClient::ListenConsole, this);

    listener.join();
    sender.join();
}

bool MailslotClient::ConnectToServer() {
    int attempts = 0;
    const int maxAttempts = 10;

    // Створюємо унікальний mailslot для кожного клієнта
    wstringstream uniqueName;
    uniqueName << MAILSLOT_BASE_NAME << m_serverId << "_" << GetCurrentProcessId();
    
    wstring clientMailslotName = uniqueName.str();

    // Створюємо mailslot для отримання повідомлень від сервера
    m_clientMailslot = CreateMailslot(
        clientMailslotName.c_str(),
        0,
        MAILSLOT_WAIT_FOREVER,
        nullptr
    );

    if (m_clientMailslot == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        wcout << L"Failed to create client mailslot. Error: " << err << endl;
        return false;
    }

    while (attempts < maxAttempts) {
        // Відкриваємо mailslot сервера для запису
        wstring serverMailslotName = wstring(MAILSLOT_BASE_NAME) + m_serverId;
        
        m_serverWriteHandle = CreateFile(
            serverMailslotName.c_str(),
            GENERIC_WRITE,
            FILE_SHARE_READ,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );

        if (m_serverWriteHandle == INVALID_HANDLE_VALUE) {
            DWORD err = GetLastError();
            wcout << L"Server not available, waiting... (attempt "
                << (attempts + 1) << L"/" << maxAttempts << L")" << endl;
            
            Sleep(1000);
            attempts++;
            continue;
        }

        break;
    }

    if (m_clientMailslot == INVALID_HANDLE_VALUE || m_serverWriteHandle == INVALID_HANDLE_VALUE) {
        return false;
    }

    return true;
}

void MailslotClient::Stop() {
    m_running = false;

    if (m_clientMailslot != INVALID_HANDLE_VALUE) {
        CloseHandle(m_clientMailslot);
        m_clientMailslot = INVALID_HANDLE_VALUE;
    }

    if (m_serverWriteHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(m_serverWriteHandle);
        m_serverWriteHandle = INVALID_HANDLE_VALUE;
    }
}

void MailslotClient::SendMessage(const wstring& message) {
    if (m_serverWriteHandle == INVALID_HANDLE_VALUE || !m_running) {
        return;
    }

    // Формат: "CLIENTMAILSLOT|USERNAME|MESSAGE"
    wstringstream uniqueName;
    uniqueName << MAILSLOT_BASE_NAME << m_serverId << L"_" << GetCurrentProcessId();
    
    wstring fullMsg = uniqueName.str() + L"|" + m_userName + L"|" + message;
    DWORD written = 0;
    DWORD msgSize = (DWORD)((fullMsg.size() + 1) * sizeof(wchar_t));

    BOOL ok = WriteFile(m_serverWriteHandle, fullMsg.c_str(), msgSize, &written, nullptr);
    if (!ok) {
        wcout << L"Failed to send message. Error: " << GetLastError() << endl;
        Stop();
    }
}

void MailslotClient::ListenConsole() {
    // Очищуємо буфер після cin
    wcin.ignore((numeric_limits<streamsize>::max)(), L'\n');

    m_currentInput.clear();

    while (m_running) {
        if (_kbhit()) {
            wchar_t ch = _getwch();

            lock_guard<mutex> lock(m_consoleMutex);

            if (ch == L'\r' || ch == L'\n') {
                // Enter
                wcout << L"\n";
                if (!m_currentInput.empty()) {
                    SendMessage(m_currentInput);
                    m_currentInput.clear();
                }
                wcout << INPUT_PROMPT << flush;
            }
            else if (ch == L'\b') {
                // Backspace
                if (!m_currentInput.empty()) {
                    m_currentInput.pop_back();
                    wcout << L"\b \b";
                }
            }
            else if (ch >= 32) {
                // Додаємо символ у поточний ввід
                m_currentInput += ch;
                wcout << ch;
            }
        }
        else {
            Sleep(10);
        }
    }
}

void MailslotClient::ListenServer() {
    wchar_t buffer[MAILSLOT_BUFFER_SIZE];

    while (m_running) {
        DWORD bytesRead = 0;
        DWORD nextSize = 0;
        DWORD messageCount = 0;

        BOOL ok = GetMailslotInfo(
            m_clientMailslot,
            nullptr,
            &nextSize,
            &messageCount,
            nullptr
        );

        if (!ok) {
            DWORD err = GetLastError();
            wcout << L"\nFailed to get mailslot info. Error: " << err << endl;
            Stop();
            break;
        }

        if (messageCount == 0 || nextSize == MAILSLOT_NO_MESSAGE) {
            Sleep(100);
            continue;
        }

        ok = ReadFile(m_clientMailslot, buffer, sizeof(buffer), &bytesRead, nullptr);

        if (!ok || bytesRead == 0) {
            DWORD err = GetLastError();

            wcout << L"\nDisconnected from server. Error: " << err << endl;

            Stop();
            break;
        }

        // Перетворюємо в wstring
        size_t charCount = bytesRead / sizeof(wchar_t);
        wstring msg(buffer, charCount);

        // Видаляємо null-термінатор
        if (!msg.empty() && msg.back() == L'\0') {
            msg.pop_back();
        }

        {
            lock_guard<mutex> lock(m_consoleMutex);
            
            // Очищуємо поточний ввід та prompt
            size_t totalLen = m_currentInput.size() + INPUT_PROMPT_LENGTH;
            for (size_t i = 0; i < totalLen; ++i) {
                wcout << L"\b \b";
            }
            
            // Виводимо повідомлення
            wcout << L"\r" << msg << endl;
            
            // Виводимо prompt та поточний ввід
            wcout << INPUT_PROMPT;
            if (!m_currentInput.empty()) {
                wcout << m_currentInput;
            }
            wcout << flush;
        }
    }
}
