#include "PipeClient.h"
#include "Config.h"
#include <iostream>
#include <limits>
#include <mutex>
#include <conio.h>

using namespace std;

constexpr int INPUT_PROMPT_LENGTH = sizeof(INPUT_PROMPT) / sizeof(wchar_t) - 1;

PipeClient::PipeClient() : m_running(false), m_readPipe(INVALID_HANDLE_VALUE), m_writePipe(INVALID_HANDLE_VALUE) {}

PipeClient::~PipeClient() {
    Stop();
}

void PipeClient::Run() {
    m_running = true;

    string id, name;
    cout << "Enter Server ID: ";
    cin >> id;
    cout << "Enter your name: ";
    cin >> name;

    m_userName = wstring(name.begin(), name.end());
    wstring basePipeName = wstring(PIPE_BASE_NAME) + wstring(id.begin(), id.end());

    // Підключаємося до сервера
    if (!ConnectToServer(basePipeName)) {
        cout << "Failed to connect to server." << endl;
        return;
    }

    wcout << L"Connected to server as '" << m_userName << L"'" << endl;
    wcout << L"Start chatting" << endl;
    wcout << INPUT_PROMPT << flush;

    // Запускаємо потоки
    thread listener(&PipeClient::ListenServer, this);
    thread sender(&PipeClient::ListenConsole, this);

    listener.join();
    sender.join();
}

bool PipeClient::ConnectToServer(const wstring& basePipeName) {
    int attempts = 0;
    const int maxAttempts = 10;

    wstring readPipeName = basePipeName + L"_write";  // клієнт читає з write pipe сервера
    wstring writePipeName = basePipeName + L"_read";  // клієнт пише в read pipe сервера

    while (attempts < maxAttempts) {
        // Підключаємося до write pipe (для читання)
        m_readPipe = CreateFile(
            readPipeName.c_str(),
            GENERIC_READ,
            0,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr
        );

        if (m_readPipe == INVALID_HANDLE_VALUE) {
            DWORD err = GetLastError();
            if (err == ERROR_PIPE_BUSY) {
                wcout << L"Pipe is busy, waiting..." << endl;
                if (!WaitNamedPipe(readPipeName.c_str(), 2000)) {
                    wcout << L"Timeout waiting for read pipe." << endl;
                    attempts++;
                    continue;
                }
            }
            else {
                wcout << L"Server not available, waiting... (attempt "
                    << (attempts + 1) << L"/" << maxAttempts << L")" << endl;
                Sleep(1000);
                attempts++;
                continue;
            }
        }

        // Підключаємося до read pipe (для запису)
        m_writePipe = CreateFile(
            writePipeName.c_str(),
            GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr
        );

        if (m_writePipe == INVALID_HANDLE_VALUE) {
            DWORD err = GetLastError();
            wcout << L"Failed to connect to write pipe. Error: " << err << endl;
            CloseHandle(m_readPipe);
            m_readPipe = INVALID_HANDLE_VALUE;

            if (err == ERROR_PIPE_BUSY) {
                if (!WaitNamedPipe(writePipeName.c_str(), 2000)) {
                    wcout << L"Timeout waiting for write pipe." << endl;
                }
            }

            Sleep(1000);
            attempts++;
            continue;
        }

        // Обидва pipe підключені
        break;
    }

    if (m_readPipe == INVALID_HANDLE_VALUE || m_writePipe == INVALID_HANDLE_VALUE) {
        return false;
    }

    // Встановлюємо режим читання message mode (опціонально, pipe вже може бути в цьому режимі)
    DWORD mode = PIPE_READMODE_MESSAGE;
    SetNamedPipeHandleState(m_readPipe, &mode, nullptr, nullptr);
    // Ігноруємо помилку, оскільки pipe може вже бути в правильному режимі

    // Відправляємо ім'я як перше повідомлення
    DWORD written = 0;
    DWORD nameSize = (DWORD)((m_userName.size() + 1) * sizeof(wchar_t));
    auto ok = WriteFile(m_writePipe, m_userName.c_str(), nameSize, &written, nullptr);

    if (!ok) {
        wcout << L"Failed to send name to server." << endl;
        CloseHandle(m_readPipe);
        CloseHandle(m_writePipe);
        m_readPipe = INVALID_HANDLE_VALUE;
        m_writePipe = INVALID_HANDLE_VALUE;
        return false;
    }

    return true;
}

void PipeClient::Stop() {
    m_running = false;

    if (m_readPipe != INVALID_HANDLE_VALUE) {
        CloseHandle(m_readPipe);
        m_readPipe = INVALID_HANDLE_VALUE;
    }

    if (m_writePipe != INVALID_HANDLE_VALUE) {
        CloseHandle(m_writePipe);
        m_writePipe = INVALID_HANDLE_VALUE;
    }
}

void PipeClient::SendMessage(const wstring& message) {
    if (m_writePipe == INVALID_HANDLE_VALUE || !m_running) {
        return;
    }

    DWORD written = 0;
    DWORD msgSize = (DWORD)((message.size() + 1) * sizeof(wchar_t));

    BOOL ok = WriteFile(m_writePipe, message.c_str(), msgSize, &written, nullptr);

    if (!ok) {
        wcout << L"Failed to send message. Error: " << GetLastError() << endl;
        Stop();
    }
}

void PipeClient::ListenConsole() {
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

void PipeClient::ListenServer() {
    wchar_t buffer[PIPE_BUFFER_SIZE];

    while (m_running) {
        DWORD bytesRead = 0;
        BOOL ok = ReadFile(m_readPipe, buffer, sizeof(buffer), &bytesRead, nullptr);

        if (!ok || bytesRead == 0) {
            DWORD err = GetLastError();

            if (err == ERROR_BROKEN_PIPE) {
                wcout << L"\nServer closed the connection." << endl;
            }
            else {
                wcout << L"\nDisconnected from server. Error: " << err << endl;
            }

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