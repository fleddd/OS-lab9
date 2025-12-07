#pragma once

constexpr wchar_t PIPE_BASE_NAME[] = L"\\\\.\\pipe\\MyPipe_";
constexpr int PIPE_BUFFER_SIZE = 512;
constexpr int MAX_CLIENTS = 10;

constexpr wchar_t MAILSLOT_BASE_NAME[] = L"\\\\.\\mailslot\\MyMailslot_";
constexpr int MAILSLOT_BUFFER_SIZE = 512;

constexpr wchar_t INPUT_PROMPT[] = L"> ";
constexpr int INPUT_PROMPT_LENGTH = sizeof(INPUT_PROMPT) / sizeof(wchar_t) - 1;

struct ClientInfo {
    HANDLE hPipe;
};
