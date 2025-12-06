#pragma once

constexpr wchar_t PIPE_BASE_NAME[] = L"\\\\.\\pipe\\MyPipe_";
constexpr int PIPE_BUFFER_SIZE = 512;
constexpr int MAX_CLIENTS = 10;

struct ClientInfo {
    HANDLE hPipe;
};
