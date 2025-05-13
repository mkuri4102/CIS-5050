#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <vector>
#include <mutex>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

/*
 * Load Balancer Implementation
 * 
 * Distributes client requests across multiple backend server instances.
 * Implements:
 * - Round-robin load distribution algorithm
 * - HTTP redirection of clients to backend servers
 * - Dynamic server registration
 * - Connection tracking per server
 * - Thread-safe server management with mutex protection
 * - Logging with timestamps
 * - Signal handling for graceful shutdown
 */

#define LB_PORT 8080         
#define BASE_SERVER_PORT 8081 
#define BUFFER_SIZE 8192
#define MAX_CONNECTIONS 100
#define MAX_REPLICAS 10    

int lb_socket;
std::vector<struct ServerInfo> servers;
std::mutex servers_mutex;
std::mutex log_mutex;
int current_server_idx = 0;

struct ServerInfo {
    std::string ip;
    int port;
    int active_connections;

    ServerInfo(const std::string& ip, int port) 
        : ip(ip), port(port), active_connections(0) {}
};

std::string get_timestamp() {
    auto now = std::time(nullptr);
    auto tm = *std::localtime(&now);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

void log_message(const std::string& message) {
    std::lock_guard<std::mutex> lock(log_mutex);
    std::string timestamp = get_timestamp();
    std::cout << "[" << timestamp << "] " << message << std::endl;
}

void cleanup_and_exit(int) {
    log_message("Load Balancer shutting down...");
    close(lb_socket);
    exit(0);
}

int get_next_server() {
    std::lock_guard<std::mutex> lock(servers_mutex);
    if (servers.empty()) {
        return -1;
    }
    
    int idx = current_server_idx;
    current_server_idx = (current_server_idx + 1) % servers.size();
    
    return idx;
}

void* handle_client(void* arg) {
    int client_fd = *(int*)arg;
    free(arg);
    
    struct sockaddr_in client_addr;
    socklen_t len = sizeof(client_addr);
    getpeername(client_fd, (struct sockaddr*)&client_addr, &len);
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
    int client_port = ntohs(client_addr.sin_port);
    std::string client_ip_str(client_ip);
    
    char buffer[BUFFER_SIZE];
    int bytes_read = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);
    if (bytes_read <= 0) {
        close(client_fd);
        return NULL;
    }
    buffer[bytes_read] = '\0';
    
    std::string request(buffer);
    std::string path_with_query = "/";  
    
    size_t path_start = request.find("GET ");
    if (path_start != std::string::npos) {
        path_start += 4; 
        size_t path_end = request.find(" ", path_start);
        if (path_end != std::string::npos) {
            path_with_query = request.substr(path_start, path_end - path_start);
        }
    }
    
    int server_idx = get_next_server();
    if (server_idx < 0) {
        std::ostringstream oss;
        oss << "No available servers to handle request from " << client_ip_str << ":" << client_port;
        log_message(oss.str());
        
        const char* error_response = "HTTP/1.1 503 Service Unavailable\r\n"
                                    "Content-Type: text/html\r\n"
                                    "Content-Length: 100\r\n"
                                    "\r\n"
                                    "<html><body><h1>503 Service Unavailable</h1><p>No servers available to handle your request.</p></body></html>";
        send(client_fd, error_response, strlen(error_response), 0);
        close(client_fd);
        return NULL;
    }
    
    std::string server_ip;
    int server_port;
    {
        std::lock_guard<std::mutex> lock(servers_mutex);
        server_ip = servers[server_idx].ip;
        server_port = servers[server_idx].port;
        servers[server_idx].active_connections++; 
    }
    
    std::ostringstream redirect_url;
    redirect_url << "http://" << server_ip << ":" << server_port << path_with_query;
    
    std::ostringstream http_response;
    http_response << "HTTP/1.1 302 Found\r\n"
                 << "Location: " << redirect_url.str() << "\r\n"
                 << "Content-Length: 0\r\n"
                 << "\r\n";
    
    std::string response_str = http_response.str();
    send(client_fd, response_str.c_str(), response_str.length(), 0);
    
    std::ostringstream oss;
    oss << "Redirected client " << client_ip_str << ":" << client_port 
        << " to server #" << server_idx << " (" << server_ip << ":" << server_port << path_with_query << ")";
    log_message(oss.str());
    
    close(client_fd);
    
    return NULL;
}

void register_server(const std::string& ip, int port) {
    std::lock_guard<std::mutex> lock(servers_mutex);
    for (const auto& server : servers) {
        if (server.ip == ip && server.port == port) {
            return;
        }
    }
    servers.emplace_back(ip, port);
    std::ostringstream oss;
    oss << "Registered backend server at " << ip << ":" << port;
    log_message(oss.str());
}

int main(int argc, char* argv[]) {
    signal(SIGINT, cleanup_and_exit);
    
    int num_replicas = 3;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--replicas") == 0 && i + 1 < argc) {
            num_replicas = atoi(argv[i + 1]);
            if (num_replicas <= 0 || num_replicas > MAX_REPLICAS) {
                num_replicas = 3;
            }
            i++;
        }
    }
    
    for (int i = 0; i < num_replicas; i++) {
        register_server("127.0.0.1", BASE_SERVER_PORT + i);
    }
    
    struct sockaddr_in lb_addr;
    memset(&lb_addr, 0, sizeof(lb_addr));
    
    lb_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (lb_socket < 0) {
        perror("Socket creation failed");
        exit(1);
    }
    
    int opt = 1;
    setsockopt(lb_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    lb_addr.sin_family = AF_INET;
    lb_addr.sin_port = htons(LB_PORT);
    lb_addr.sin_addr.s_addr = INADDR_ANY;
    
    if (bind(lb_socket, (struct sockaddr*)&lb_addr, sizeof(lb_addr)) < 0) {
        perror("Bind failed");
        exit(1);
    }
    
    listen(lb_socket, MAX_CONNECTIONS);
    log_message("Load Balancer (Master Node) listening on port " + std::to_string(LB_PORT));
    log_message("Directing clients to " + std::to_string(servers.size()) + " backend servers using Round Robin");
    log_message("Press Ctrl+C to stop the load balancer");
    
    while (1) {
        struct sockaddr_in client_addr;
        memset(&client_addr, 0, sizeof(client_addr));
        socklen_t addrlen = sizeof(client_addr);
        
        int* client_fd = (int*)malloc(sizeof(int));
        *client_fd = accept(lb_socket, (struct sockaddr*)&client_addr, &addrlen);
        
        if (*client_fd < 0) {
            perror("Accept failed");
            free(client_fd);
            continue;
        }
        
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        
        std::ostringstream oss;
        oss << "New client connection from " << client_ip << ":" << ntohs(client_addr.sin_port);
        log_message(oss.str());
        
        pthread_t tid;
        pthread_create(&tid, NULL, handle_client, client_fd);
        pthread_detach(tid);
    }
    
    return 0;
} 