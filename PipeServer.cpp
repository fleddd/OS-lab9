#include "PipeServer.h"
#include "Config.h"
#include <iostream>
#include <algorithm> // Потрібно для std::find_if

using namespace std;

// Конструктор
PipeServer::PipeServer() : m_running(false) {}

// Деструктор: зупиняє сервер і звільняє всі ресурси
PipeServer::~PipeServer() {
    Stop();
}

// Головний метод запуску сервера
void PipeServer::Run() {
    m_running = true;

    // Отримуємо ID поточного процесу. Це дозволяє запускати декілька серверів 
    // на одному комп'ютері, і вони матимуть унікальні імена каналів (наприклад, Pipe_1234)
    wstring id = to_wstring(GetCurrentProcessId());

    wstring basePipeName = wstring(PIPE_BASE_NAME) + id;
    wcout << L"Server ID: " << id << endl;
    wcout << L"Server started on pipes: " << basePipeName << L"_*" << endl;

    // Запускаємо потік, який буде чекати на нових клієнтів.
    // .detach() дозволяє потоку працювати у фоні незалежно від основного потоку.
    thread(&PipeServer::WaitForClients, this, basePipeName).detach();

    // Головний потік просто "живе", щоб програма не закрилася
    while (m_running) {
        Sleep(100);
    }
}

// Зупинка сервера
void PipeServer::Stop() {
    m_running = false;

    // Блокуємо м'ютекс перед доступом до списку клієнтів, щоб уникнути гонки даних
    lock_guard<mutex> lock(m_clientsMutex);

    for (auto& client : m_clients) {
        // Розриваємо з'єднання та закриваємо хендли
        DisconnectNamedPipe(client.readPipe);
        DisconnectNamedPipe(client.writePipe);
        CloseHandle(client.readPipe);
        CloseHandle(client.writePipe);
    }
    m_clients.clear();
}

// Функція розсилки повідомлень усім клієнтам (Broadcasting)
void PipeServer::BroadcastMessage(const wstring& sender, const wstring& message) {
    // Формуємо повне повідомлення: "[Name]: Hello"
    wstring fullMsg = L"[" + sender + L"]: " + message;

    // Створюємо копію списку клієнтів під захистом м'ютекса.
    // Це важливо! Ми не хочемо тримати м'ютекс заблокованим під час відправки даних (IO операції),
    // бо це заблокує підключення нових клієнтів. Тому копіюємо список і відпускаємо м'ютекс.
    vector<ClientInfo> clientsCopy;
    {
        lock_guard<mutex> lock(m_clientsMutex);
        clientsCopy = m_clients;
    }

    // Проходимо по копії списку і надсилаємо повідомлення
    for (auto& client : clientsCopy) {
        DWORD written = 0;
        DWORD msgSize = (DWORD)((fullMsg.size() + 1) * sizeof(wchar_t));

        BOOL ok = WriteFile(client.writePipe, fullMsg.c_str(), msgSize, &written, nullptr);

        // Якщо виникла помилка (наприклад, клієнт "відвалився" без попередження)
        if (!ok || written == 0) {
            DWORD err = GetLastError();
            wcout << L"Failed to send to " << client.name << L" (error: " << err << L")" << endl;

            // Якщо канал розірвано — видаляємо цього клієнта
            if (err == ERROR_BROKEN_PIPE || err == ERROR_NO_DATA) {
                RemoveClient(client.readPipe);
            }
        }
    }
}

// Видалення клієнта зі списку
void PipeServer::RemoveClient(HANDLE readPipe) {
    lock_guard<mutex> lock(m_clientsMutex); // Захист списку

    // Шукаємо клієнта за його дескриптором читання
    auto it = find_if(m_clients.begin(), m_clients.end(),
        [readPipe](const ClientInfo& c) { return c.readPipe == readPipe; });

    if (it != m_clients.end()) {
        wcout << L"Client " << it->name << L" disconnected." << endl;
        // Закриваємо дескриптори, щоб звільнити ресурси ОС
        CloseHandle(it->readPipe);
        CloseHandle(it->writePipe);
        m_clients.erase(it);
    }
}

// Потік, що очікує підключення нових клієнтів
void PipeServer::WaitForClients(const wstring& basePipeName) {
    while (m_running) {
        wstring readPipeName = basePipeName + L"_read";
        wstring writePipeName = basePipeName + L"_write";

        // --- Крок 1: Створення іменованого каналу для читання (INBOUND) ---
        // Сервер читатиме з цього каналу дані від клієнта
        HANDLE hReadPipe = CreateNamedPipe(
            readPipeName.c_str(),
            PIPE_ACCESS_INBOUND,        // Доступ: тільки читання
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, // Повідомлення, а не потік байтів
            PIPE_UNLIMITED_INSTANCES,   // Дозволяємо багато клієнтів
            PIPE_BUFFER_SIZE,           // Розмір буфера виводу
            PIPE_BUFFER_SIZE,           // Розмір буфера вводу
            0,                          // Тайм-аут за замовчуванням
            nullptr
        );

        if (hReadPipe == INVALID_HANDLE_VALUE) {
            cerr << "Failed to create read pipe. Error: " << GetLastError() << endl;
            Sleep(1000);
            continue;
        }

        // --- Крок 2: Створення іменованого каналу для запису (OUTBOUND) ---
        // Сервер писатиме в цей канал дані для клієнта
        HANDLE hWritePipe = CreateNamedPipe(
            writePipeName.c_str(),
            PIPE_ACCESS_OUTBOUND,       // Доступ: тільки запис
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

        // --- Крок 3: Очікування підключення клієнта ---
        // ConnectNamedPipe блокує виконання, поки клієнт не викличе CreateFile

        // Чекаємо на pipe читання
        BOOL connected = ConnectNamedPipe(hReadPipe, nullptr);
        if (!connected && GetLastError() != ERROR_PIPE_CONNECTED) {
            CloseHandle(hReadPipe);
            CloseHandle(hWritePipe);
            continue;
        }

        // Чекаємо на pipe запису
        connected = ConnectNamedPipe(hWritePipe, nullptr);
        if (!connected && GetLastError() != ERROR_PIPE_CONNECTED) {
            DisconnectNamedPipe(hReadPipe);
            CloseHandle(hReadPipe);
            CloseHandle(hWritePipe);
            continue;
        }

        wcout << L"Client connected, waiting for name..." << endl;

        // --- Крок 4: Рукостискання (Handshake) ---
        // Одразу після підключення читаємо перше повідомлення — це має бути ім'я
        wchar_t nameBuffer[256] = { 0 };
        DWORD bytesRead = 0;
        BOOL ok = ReadFile(hReadPipe, nameBuffer, sizeof(nameBuffer), &bytesRead, nullptr);

        if (!ok || bytesRead == 0) {
            wcout << L"Failed to read client name." << endl;
            // Якщо ім'я не прийшло — розриваємо з'єднання
            DisconnectNamedPipe(hReadPipe);
            DisconnectNamedPipe(hWritePipe);
            CloseHandle(hReadPipe);
            CloseHandle(hWritePipe);
            continue;
        }

        wstring clientName(nameBuffer);
        wcout << L"Client '" << clientName << L"' joined the chat!" << endl;

        // Додаємо клієнта до списку (безпечно для потоків)
        {
            lock_guard<mutex> lock(m_clientsMutex);
            m_clients.push_back({ hReadPipe, hWritePipe, clientName });
        }

        // Повідомляємо всіх про нового учасника
        BroadcastMessage(L"SERVER", clientName + L" joined the chat");

        // Запускаємо окремий потік для обробки повідомлень ЦЬОГО конкретного клієнта
        thread(&PipeServer::HandleClient, this, hReadPipe, hWritePipe).detach();

        // Цикл WaitForClients продовжується і створює нову пару пайпів для наступного клієнта
    }
}

// Функція обробки повідомлень одного конкретного клієнта
void PipeServer::HandleClient(HANDLE readPipe, HANDLE writePipe) {
    wchar_t buffer[PIPE_BUFFER_SIZE];

    // Знаходимо ім'я клієнта для логування (шукаємо в списку)
    wstring clientName;
    {
        lock_guard<mutex> lock(m_clientsMutex);
        auto it = find_if(m_clients.begin(), m_clients.end(),
            [readPipe](const ClientInfo& c) { return c.readPipe == readPipe; });
        if (it != m_clients.end()) {
            clientName = it->name;
        }
    }

    // Головний цикл читання повідомлень від клієнта
    while (m_running) {
        DWORD bytesRead = 0;
        // Блокуюче читання з каналу
        BOOL ok = ReadFile(readPipe, buffer, sizeof(buffer), &bytesRead, nullptr);

        // Якщо помилка читання або 0 байт — клієнт відключився
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

        // --- Логіка ХАБА (Hub) ---
        // Сервер не друкує повідомлення собі в консоль для читання адміном.
        // Його задача — взяти повідомлення від одного і переслати ВCІМ.
        BroadcastMessage(clientName, msg);
    }

    // Якщо ми вийшли з циклу (клієнт відключився):
    DisconnectNamedPipe(readPipe);
    DisconnectNamedPipe(writePipe);
    RemoveClient(readPipe); // Видаляємо зі списку

    // Повідомляємо інших, що учасник вийшов
    BroadcastMessage(L"SERVER", clientName + L" left the chat");
}