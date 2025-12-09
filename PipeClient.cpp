#include "PipeClient.h"
#include "Config.h"
#include <iostream>
#include <limits>
#include <mutex>
#include <conio.h> // Потрібна для _kbhit() та _getwch()

using namespace std;

// Вираховуємо довжину запрошення вводу (наприклад "Me: "), щоб правильно стирати рядки
constexpr int INPUT_PROMPT_LENGTH = sizeof(INPUT_PROMPT) / sizeof(wchar_t) - 1;

// Конструктор: ініціалізуємо прапорці та хендли (дескриптори) значеннями "невалідний"
PipeClient::PipeClient() : m_running(false), m_readPipe(INVALID_HANDLE_VALUE), m_writePipe(INVALID_HANDLE_VALUE) {}

// Деструктор: гарантує зупинку клієнта та закриття каналів при видаленні об'єкта
PipeClient::~PipeClient() {
    Stop();
}

// Головний метод запуску клієнта
void PipeClient::Run() {
    m_running = true;

    string id, name;
    cout << "Enter Server ID: ";
    cin >> id;
    cout << "Enter your name: ";
    cin >> name;

    // Конвертуємо string у wstring (широкі символи для Windows API)
    m_userName = wstring(name.begin(), name.end());
    // Формуємо базове ім'я пайпу, наприклад: "\\.\pipe\ChatPipe_1"
    wstring basePipeName = wstring(PIPE_BASE_NAME) + wstring(id.begin(), id.end());

    // Спроба підключення до сервера
    if (!ConnectToServer(basePipeName)) {
        cout << "Failed to connect to server." << endl;
        return;
    }

    wcout << L"Connected to server as '" << m_userName << L"'" << endl;
    wcout << L"Start chatting" << endl;
    wcout << INPUT_PROMPT << flush;

    // Запускаємо два окремі потоки:
    // 1. listener - постійно слухає повідомлення від сервера
    // 2. sender - слухає натискання клавіш користувача
    thread listener(&PipeClient::ListenServer, this);
    thread sender(&PipeClient::ListenConsole, this);

    // Очікуємо завершення потоків (блокуємо main, поки програма працює)
    listener.join();
    sender.join();
}

// Логіка підключення до іменованих каналів
bool PipeClient::ConnectToServer(const wstring& basePipeName) {
    int attempts = 0;
    const int maxAttempts = 10;

    // Важливо: Назви каналів мають бути "дзеркальними" до сервера.
    // Те, куди сервер пише (_write), клієнт має відкрити для читання.
    // Те, звідки сервер читає (_read), клієнт має відкрити для запису.
    wstring readPipeName = basePipeName + L"_write";  // Читаємо звідси
    wstring writePipeName = basePipeName + L"_read";  // Пишемо сюди

    while (attempts < maxAttempts) {
        // --- Крок 1: Підключення до каналу для читання ---
        m_readPipe = CreateFile(
            readPipeName.c_str(),   // Ім'я пайпу
            GENERIC_READ,           // Режим доступу: читання
            0,                      // Режим спільного доступу (0 - ексклюзивно)
            nullptr,                // Атрибути безпеки
            OPEN_EXISTING,          // Відкриваємо тільки якщо існує
            0,                      // Атрибути файлу
            nullptr
        );

        // Якщо не вдалося підключитися
        if (m_readPipe == INVALID_HANDLE_VALUE) {
            DWORD err = GetLastError();
            // Якщо пайп зайнятий (сервер ще не викликав ConnectNamedPipe або ліміт клієнтів)
            if (err == ERROR_PIPE_BUSY) {
                wcout << L"Pipe is busy, waiting..." << endl;
                // Чекаємо звільнення пайпу 2 секунди
                if (!WaitNamedPipe(readPipeName.c_str(), 2000)) {
                    wcout << L"Timeout waiting for read pipe." << endl;
                    attempts++;
                    continue;
                }
            }
            else {
                // Сервер взагалі не запущений або інша помилка
                wcout << L"Server not available, waiting... (attempt "
                    << (attempts + 1) << L"/" << maxAttempts << L")" << endl;
                Sleep(1000); // Чекаємо 1 секунду перед повтором
                attempts++;
                continue;
            }
        }

        // --- Крок 2: Підключення до каналу для запису ---
        m_writePipe = CreateFile(
            writePipeName.c_str(),
            GENERIC_WRITE,          // Режим доступу: запис
            0,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr
        );

        // Якщо не вдалося відкрити канал запису, треба закрити вже відкритий канал читання
        if (m_writePipe == INVALID_HANDLE_VALUE) {
            DWORD err = GetLastError();
            wcout << L"Failed to connect to write pipe. Error: " << err << endl;
            CloseHandle(m_readPipe); // Закриваємо m_readPipe, щоб не було витоку ресурсів
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

        // Якщо обидва канали успішно відкриті — виходимо з циклу
        break;
    }

    if (m_readPipe == INVALID_HANDLE_VALUE || m_writePipe == INVALID_HANDLE_VALUE) {
        return false;
    }

    // --- Налаштування режиму передачі ---
    // Встановлюємо режим PIPE_READMODE_MESSAGE. Це дозволяє читати дані пакетами (повідомленнями),
    // а не потоком байтів (як у TCP).
    DWORD mode = PIPE_READMODE_MESSAGE;
    SetNamedPipeHandleState(m_readPipe, &mode, nullptr, nullptr);

    // --- Рукостискання (Handshake) ---
    // Відразу після підключення відправляємо серверу своє ім'я
    DWORD written = 0;
    DWORD nameSize = (DWORD)((m_userName.size() + 1) * sizeof(wchar_t)); // +1 для null-термінатора
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

// Зупинка роботи та звільнення ресурсів
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

// Функція відправки повідомлення на сервер
void PipeClient::SendMessage(const wstring& message) {
    if (m_writePipe == INVALID_HANDLE_VALUE || !m_running) {
        return;
    }

    DWORD written = 0;
    // Обчислюємо розмір у байтах (кількість символів + \0 * розмір wchar_t)
    DWORD msgSize = (DWORD)((message.size() + 1) * sizeof(wchar_t));

    BOOL ok = WriteFile(m_writePipe, message.c_str(), msgSize, &written, nullptr);

    if (!ok) {
        wcout << L"Failed to send message. Error: " << GetLastError() << endl;
        Stop();
    }
}

// Потік, що обробляє ввід з клавіатури
void PipeClient::ListenConsole() {
    // Очищуємо вхідний буфер cin, щоб сміття після введення імені не потрапило в чат
    wcin.ignore((numeric_limits<streamsize>::max)(), L'\n');

    m_currentInput.clear();

    while (m_running) {
        // _kbhit() перевіряє, чи натиснута клавіша, не блокуючи виконання програми
        if (_kbhit()) {
            wchar_t ch = _getwch(); // Зчитуємо натиснутий символ без виводу на екран

            // Блокуємо консоль м'ютексом, щоб вивід не змішувався з повідомленнями від сервера
            lock_guard<mutex> lock(m_consoleMutex);

            if (ch == L'\r' || ch == L'\n') {
                // Натиснуто Enter
                wcout << L"\n";
                if (!m_currentInput.empty()) {
                    SendMessage(m_currentInput); // Відправляємо
                    m_currentInput.clear();      // Очищуємо буфер
                }
                wcout << INPUT_PROMPT << flush; // Малюємо нове запрошення
            }
            else if (ch == L'\b') {
                // Натиснуто Backspace
                if (!m_currentInput.empty()) {
                    m_currentInput.pop_back();
                    // Емуляція видалення символу в консолі: крок назад, пробіл, крок назад
                    wcout << L"\b \b";
                }
            }
            else if (ch >= 32) {
                // Звичайні друковані символи
                m_currentInput += ch;
                wcout << ch;
            }
        }
        else {
            // Якщо клавіша не натиснута, трохи спимо, щоб не вантажити процесор (CPU usage)
            Sleep(10);
        }
    }
}

// Потік, що слухає повідомлення від сервера
void PipeClient::ListenServer() {
    wchar_t buffer[PIPE_BUFFER_SIZE];

    while (m_running) {
        DWORD bytesRead = 0;
        // ReadFile - блокуюча операція (чекає поки прийдуть дані)
        BOOL ok = ReadFile(m_readPipe, buffer, sizeof(buffer), &bytesRead, nullptr);

        if (!ok || bytesRead == 0) {
            DWORD err = GetLastError();

            // ERROR_BROKEN_PIPE означає, що сервер розірвав з'єднання
            if (err == ERROR_BROKEN_PIPE) {
                wcout << L"\nServer closed the connection." << endl;
            }
            else {
                wcout << L"\nDisconnected from server. Error: " << err << endl;
            }

            Stop();
            break;
        }

        // Конвертуємо байти назад у рядок
        size_t charCount = bytesRead / sizeof(wchar_t);
        wstring msg(buffer, charCount);

        // Прибираємо зайвий null-термінатор в кінці, якщо він є
        if (!msg.empty() && msg.back() == L'\0') {
            msg.pop_back();
        }

        // Оновлюємо інтерфейс користувача (thread-safe)
        {
            lock_guard<mutex> lock(m_consoleMutex);

            // Хитрість для красивого чату:
            // 1. Стираємо поточний рядок, де користувач щось пише
            size_t totalLen = m_currentInput.size() + INPUT_PROMPT_LENGTH;
            for (size_t i = 0; i < totalLen; ++i) {
                wcout << L"\b \b";
            }

            // 2. Виводимо нове повідомлення від сервера
            // \r повертає курсор на початок рядка
            wcout << L"\r" << msg << endl;

            // 3. Відновлюємо запрошення та те, що користувач встиг написати
            wcout << INPUT_PROMPT;
            if (!m_currentInput.empty()) {
                wcout << m_currentInput;
            }
            wcout << flush;
        }
    }
}