#include <iostream>
#include <string>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/socket.h>

using namespace std;

const int BUFF_SIZE = 1000;

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "*** Author: Minna Kuriakose (mkuri)\n");
        exit(1);
    }

    // Create a UDP socket
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        cerr << "-ERR Unable to open socket\n";
        exit(2);
    }

    // Allow reuse of the address
    const int REUSE = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &REUSE, sizeof(REUSE));

    // Parse server IP and port from input argument
    sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;

    char* addr = strtok(argv[1], ":");
    char* port = strtok(NULL, ":");

    if (!addr || !port || inet_pton(AF_INET, addr, &serverAddr.sin_addr) <= 0) {
        cerr << "-ERR Invalid server address\n";
        exit(1);
    }

    serverAddr.sin_port = htons(atoi(port));

    // For receiving messages
    sockaddr_in fromAddr;
    socklen_t fromLen = sizeof(fromAddr);

    while (true) {
        fd_set readySet;
        FD_ZERO(&readySet);
        FD_SET(STDIN_FILENO, &readySet); // Keyboard input
        FD_SET(sock, &readySet);         // Socket input

        // Wait until input is ready on stdin or socket
        if (select(sock + 1, &readySet, nullptr, nullptr, nullptr) < 0) {
            cerr << "-ERR select() failed\n";
            break;
        }

        char buffer[BUFF_SIZE];
        int bytesRead;

        // Handle user input
        if (FD_ISSET(STDIN_FILENO, &readySet)) {
            bytesRead = read(STDIN_FILENO, buffer, BUFF_SIZE);
            if (bytesRead <= 0) continue;

            buffer[bytesRead - 1] = '\0'; // Replace newline with null terminator

            sendto(sock, buffer, strlen(buffer), 0,
                   (sockaddr*)&serverAddr, sizeof(serverAddr));

            char* cmd = strtok(buffer, " ");
            if (strcasecmp(cmd, "/quit") == 0) {
                break;
            }

        } else if (FD_ISSET(sock, &readySet)) {
            // Handle message from server
            bytesRead = recvfrom(sock, buffer, BUFF_SIZE - 1, 0,
                                 (sockaddr*)&fromAddr, &fromLen);
            if (bytesRead > 0) {
                buffer[bytesRead] = '\0';
                cout << buffer << endl;
            }
        }

        memset(buffer, 0, BUFF_SIZE);
    }

    close(sock);
    return 0;
}
