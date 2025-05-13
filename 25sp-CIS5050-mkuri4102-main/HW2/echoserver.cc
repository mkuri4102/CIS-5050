#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <cstring>
#include <string>
#include <string.h>

#define DEFAULT_PORT 10000
#define BUFFER_SIZE 1024
#define MAX_CONNECTIONS 100

int server_socket;
int verbose = 0;

// Struct for passing client info to threads
typedef struct {
    int client_fd;
    struct sockaddr_in client_addr;
} client_info;

void* handle_client(void* arg);
void cleanup_and_exit(int signum);

int main(int argc, char *argv[]) {
    int port = DEFAULT_PORT;
    struct sockaddr_in server_addr;

    // Parse command-line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-a") == 0) {
            fprintf(stderr, "Minna Kuriakose - mkuri\n");
            exit(0);
        } else if (strcmp(argv[i], "-v") == 0) {
            verbose = 1;
        }
    }

    // Set up signal handler for cleanup
    signal(SIGINT, cleanup_and_exit);

    // Create server socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    // Bind the socket
    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    // Start listening
    if (listen(server_socket, MAX_CONNECTIONS) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    //printf("Server listening on port %d\n", port);

    while (1) {
        client_info* cinfo = (client_info*) malloc(sizeof(client_info));
        socklen_t addr_len = sizeof(cinfo->client_addr);
        cinfo->client_fd = accept(server_socket, (struct sockaddr*)&cinfo->client_addr, &addr_len);
        if (cinfo->client_fd < 0) {
            perror("Accept failed");
            free(cinfo);
            continue;
        }

        if (verbose) printf("[%d] New connection\n", cinfo->client_fd);

        // Send greeting message
        //send(cinfo->client_fd, "+OK Server ready (Author: Minna Kuriakose / mkuri)\r\n", 50, 0);
        send(cinfo->client_fd, "+OK Server ready (Author: Minna Kuriakose / mkuri)", 45, 0);


        pthread_t thread;
        pthread_create(&thread, NULL, handle_client, cinfo);
        pthread_detach(thread);
    }
    return 0;
}

void* handle_client(void* arg) {
    client_info* cinfo = (client_info*)arg;
    int client_fd = cinfo->client_fd;  // Store client file descriptor
    free(cinfo);  // Free the dynamically allocated memory immediately

    char recv_buffer[BUFFER_SIZE] = {0};  // Buffer to store incoming data
    size_t recv_len = 0;  // Track received data length

    // Send the initial greeting message expected by `echo_test`
    //std::string msg = "+OK Server ready (Author: Minna Kuriakose / mkuri)\r\n";
    //int length = send(client_fd, msg.c_str(), msg.length(), 0);
    send(client_fd, "+OK Server ready (Author: Minna Kuriakose / mkuri)\r\n", 52, 0);
    //printf("[%d] send response", length);
    if (verbose) printf("[%d] S: +OK Server ready (Author: Minna Kuriakose / mkuri)\n", client_fd);

    while (1) {
        char buffer[BUFFER_SIZE] = {0};  // Temporary buffer for receiving data
        int bytes_read = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);

        if (bytes_read <= 0) {
            if (verbose) printf("[%d] Connection closed\n", client_fd);
            break;  // Exit loop if client disconnects or an error occurs
        }

        buffer[bytes_read] = '\0';  // Ensure null-terminated string

        // Append new data to the existing receive buffer
        strncat(recv_buffer, buffer, sizeof(recv_buffer) - strlen(recv_buffer) - 1);
        recv_len += bytes_read;

        if (verbose) printf("[%d] C: %s", client_fd, recv_buffer);

        // Process full commands (commands should end with "\r\n")
        char* command_end;
        while ((command_end = strstr(recv_buffer, "\r\n")) != NULL) {
            *command_end = '\0';  // Replace "\r\n" with null terminator

            //if (verbose) printf("[%d] Processing command: %s\n", client_fd, recv_buffer);

            // Handle ECHO command
            if (strncasecmp(recv_buffer, "ECHO ", 5) == 0) {
                char message[BUFFER_SIZE];
                snprintf(message, sizeof(message), "+OK %s\r\n", recv_buffer + 5);
                send(client_fd, message, strlen(message), 0);
                if (verbose) printf("[%d] S: %s", client_fd, message);
            }
            // Handle QUIT command
            else if (strcasecmp(recv_buffer, "QUIT") == 0) {
                send(client_fd, "+OK Goodbye!\r\n", 15, 0);
                if (verbose) printf("[%d] S: +OK Goodbye!\n", client_fd);
                close(client_fd);  // Close connection after QUIT
                return NULL;
            }
            // Handle Unknown Commands
            else {
            	send(client_fd, "-ERR Unknown command\r\n", strlen("-ERR Unknown command\r\n"), 0);
            	//send(client_fd, "-ERR Unknown command\r\n", 23, 0);
                if (verbose) printf("[%d] S: -ERR Unknown command\n", client_fd);
            }

            // Move remaining input data forward (in case of multiple commands)
            memmove(recv_buffer, command_end + 2, strlen(command_end + 2) + 1);
            recv_len = strlen(recv_buffer);
        }
    }

    close(client_fd);  // Close connection when done
    return NULL;
}


void cleanup_and_exit(int signum) {
    printf("Shutting down server...\n");
    close(server_socket);
    exit(0);
}
