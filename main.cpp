#include <iostream>
#include <memory>
#include <string>

#include "IServer.h"
#include "IClient.h"
#include "PipeServer.h"
#include "PipeClient.h"
#include "MailslotServer.h"
#include "MailslotClient.h"

using namespace std;

int main() {
    cout << "=== Chat Program ===\n";
    cout << "Select role:\n1. Server\n2. Client\nChoice: ";

    int roleChoice = 0;
    cin >> roleChoice;

    cout << "Select IPC method:\n1. Pipe\n2. Mailslot\nChoice: ";
    int ipcChoice = 0;
    cin >> ipcChoice;

    unique_ptr<IServer> server;
    unique_ptr<IClient> client;

    if (roleChoice == 1) { // Server
        if (ipcChoice == 1) {
            server = make_unique<PipeServer>();
        }
        else if (ipcChoice == 2) {
            server = make_unique<MailslotServer>();
        }
        else {
            cerr << "Invalid IPC choice.\n";
            return 1;
        }
        server->Run();
    }
    else if (roleChoice == 2) { // Client
        if (ipcChoice == 1) {
            client = make_unique<PipeClient>();
        }
        else if (ipcChoice == 2) {
            client = make_unique<MailslotClient>();
        }
        else {
            cerr << "Invalid IPC choice.\n";
            return 1;
        }
        client->Run();
    }
    else {
        cerr << "Invalid role choice.\n";
        return 1;
    }

    return 0;
}
