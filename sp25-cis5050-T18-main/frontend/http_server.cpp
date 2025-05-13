#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <sstream>
#include <string>
#include <iostream>
#include <fstream>
#include <map>
#include <sys/time.h>
#include <fcntl.h>

/*
 * HTTP Server Implementation
 * 
 * Main HTTP server for the web application.
 * Implements:
 * - Multi-threaded request handling
 * - Socket communication
 * - Request parsing and routing to appropriate handlers
 * - Keep-alive connection management
 * - Support for GET, POST, and HEAD methods
 * - Signal handling for graceful shutdown
 * - Non-blocking I/O for timeout management
 */

#include "include/handlers/handlers.h"
#include "include/routes/routes.h"
#include "include/utils.h"

#define DEFAULT_PORT 8080
#define BUFFER_SIZE 8192
#define MAX_CONNECTIONS 100
#define KV_SERVER_PORT 5050
#define KV_SERVER_IP "127.0.0.1"
#define KEEP_ALIVE_TIMEOUT 15 // Timeout in seconds for keep-alive connections

int server_socket;
int kv_socket = -1;
pthread_mutex_t kv_mutex = PTHREAD_MUTEX_INITIALIZER;

// Parse Connection header from HTTP request
bool check_keep_alive(const char* buffer) {
    std::string headers(buffer);
    std::istringstream iss(headers);
    std::string line;
    
    // Skip the request line
    std::getline(iss, line);
    
    // Parse headers
    while (std::getline(iss, line) && line != "\r" && line != "") {
        // Remove trailing \r if present
        if (!line.empty() && line[line.size() - 1] == '\r') {
            line.erase(line.size() - 1);
        }
        
        // Check for Connection header
        if (line.find("Connection:") == 0 || line.find("connection:") == 0) {
            std::string value = line.substr(line.find(":") + 1);
            // Trim leading spaces
            value = value.substr(value.find_first_not_of(" "));
            return (value == "keep-alive");
        }
    }
    
    // Default to close if no Connection header is found
    return false;
}

// Set non-blocking mode on a socket
void set_nonblocking(int sock) {
    int flags = fcntl(sock, F_GETFL, 0);
    flags |= O_NONBLOCK;
    fcntl(sock, F_SETFL, flags);
}

// Set socket back to blocking mode
void set_blocking(int sock) {
    int flags = fcntl(sock, F_GETFL, 0);
    flags &= ~O_NONBLOCK;
    fcntl(sock, F_SETFL, flags);
}

void* handle_client(void* arg) {
    int client_fd = *(int*)arg;
    free(arg);
    char buffer[BUFFER_SIZE];
    bool keep_alive = false;
    
    // Set timeout for keep-alive connections
    struct timeval timeout;
    timeout.tv_sec = KEEP_ALIVE_TIMEOUT;
    timeout.tv_usec = 0;
    
    do {
        memset(buffer, 0, BUFFER_SIZE);
        
        // Set socket to non-blocking for timeout handling
        set_nonblocking(client_fd);
        
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(client_fd, &read_fds);
        
        // Wait for data with timeout
        int select_result = select(client_fd + 1, &read_fds, NULL, NULL, &timeout);
        
        if (select_result <= 0) {
            // Timeout or error occurred
            break;
        }
        
        // Set socket back to blocking for regular operations
        set_blocking(client_fd);
        
        int bytes = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);
        if (bytes <= 0) {
            // Connection closed or error
            break;
        }
        
        buffer[bytes] = '\0';
        
        // Check if client wants to keep the connection alive
        keep_alive = check_keep_alive(buffer);
        
        std::string method, path;
        if (Utils::parse_http_request(buffer, method, path)) {
            if (method == "GET") {
                Routes::handle_get_request(client_fd, path, buffer, keep_alive);
            } else if (method == "POST") {
                Routes::handle_post_request(client_fd, path, buffer, keep_alive);
            } else if (method == "HEAD") {
                Routes::handle_head_request(client_fd, path, buffer, keep_alive);
            } else {
                std::string not_implemented_response = "HTTP/1.1 501 Not Implemented\r\n"
                                                      "Content-Type: text/html\r\n"
                                                      "Content-Length: 129\r\n";
                if (keep_alive) {
                    not_implemented_response += "Connection: keep-alive\r\n"
                                               "Keep-Alive: timeout=" + std::to_string(KEEP_ALIVE_TIMEOUT) + "\r\n";
                } else {
                    not_implemented_response += "Connection: close\r\n";
                }
                not_implemented_response += "\r\n"
                                          "<html><body><h1>501 Not Implemented</h1><p>The method " + method + " is not implemented by this server.</p></body></html>";
                send(client_fd, not_implemented_response.c_str(), not_implemented_response.size(), 0);
            }
        } else {
            std::string bad_request_response = "HTTP/1.1 400 Bad Request\r\n"
                                              "Content-Type: text/html\r\n"
                                              "Content-Length: 79\r\n";
            if (keep_alive) {
                bad_request_response += "Connection: keep-alive\r\n"
                                       "Keep-Alive: timeout=" + std::to_string(KEEP_ALIVE_TIMEOUT) + "\r\n";
            } else {
                bad_request_response += "Connection: close\r\n";
            }
            bad_request_response += "\r\n"
                                  "<html><body><h1>400 Bad Request</h1><p>Bad request syntax.</p></body></html>";
            send(client_fd, bad_request_response.c_str(), bad_request_response.size(), 0);
        }
        
        // Reset the timeout for the next request
        timeout.tv_sec = KEEP_ALIVE_TIMEOUT;
        timeout.tv_usec = 0;
        
    } while (keep_alive);
    
    // Close the connection
    close(client_fd);
    return NULL;
}

void cleanup_and_exit(int) {
    printf("HTTP Server shutting down...\n");
    pthread_mutex_lock(&kv_mutex);
    if (kv_socket >= 0) {
        close(kv_socket);
        kv_socket = -1;
    }
    pthread_mutex_unlock(&kv_mutex);
    close(server_socket);
    _exit(0);
}

int main(int argc, char* argv[]) {
    signal(SIGINT, cleanup_and_exit);
    
    int port = DEFAULT_PORT;
    if (argc > 1) {
        port = atoi(argv[1]);
        if (port <= 0 || port > 65535) {
            fprintf(stderr, "Invalid port number: %s\n", argv[1]);
            fprintf(stderr, "Using default port %d\n", DEFAULT_PORT);
            port = DEFAULT_PORT;
        }
    }
    
    if (!Utils::connect_to_kvstore()) {
        fprintf(stderr, "Warning: Failed to connect to KVStore server. Some functionality may be limited.\n");
    }
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Socket creation failed");
        exit(1);
    }
    
    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(1);
    }
    
    listen(server_socket, MAX_CONNECTIONS);
    printf("HTTP Server listening on port %d\n", port);
    printf("You can access the following pages:\n");
    printf("  - Home: http://localhost:%d/\n", port);
    printf("Press Ctrl+C to stop the server\n");
    
    while (1) {
        struct sockaddr_in client_addr;
        memset(&client_addr, 0, sizeof(client_addr));
        socklen_t addrlen = sizeof(client_addr);
        
        int* client_fd = (int*)malloc(sizeof(int));
        *client_fd = accept(server_socket, (struct sockaddr*)&client_addr, &addrlen);
        
        if (*client_fd < 0) {
            perror("Accept failed");
            free(client_fd);
            continue;
        }
        
        pthread_t tid;
        pthread_create(&tid, NULL, handle_client, client_fd);
        pthread_detach(tid);
    }
    
    return 0;
}

