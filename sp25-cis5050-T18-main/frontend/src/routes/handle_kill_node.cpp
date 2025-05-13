#include "include/routes/routes.h"
#include <string>
#include <sstream>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <iostream>
#include <cstring>

/*
 * Kill Node Handler
 * 
 * Provides functionality for the admin dashboard to terminate a specific node.
 * Implements:
 * - Node identification by ID
 * - Socket connection to the target node
 * - Sending KILL command to gracefully terminate the node
 * - Error handling for connection failures
 */

// Keep alive timeout in seconds
#define KEEP_ALIVE_TIMEOUT 15

namespace Routes {

    void handle_kill_node(int client_fd, const std::string& body, bool keep_alive) {
        size_t pos = body.find("nodeId=");
        if (pos == std::string::npos) {
            std::string error_msg = "{\"error\":\"Missing nodeId parameter\"}";
            std::string response = "HTTP/1.1 400 Bad Request\r\n"
                                  "Content-Type: application/json\r\n"
                                  "Content-Length: " + std::to_string(error_msg.length()) + "\r\n";
            
            if (keep_alive) {
                response += "Connection: keep-alive\r\n"
                           "Keep-Alive: timeout=" + std::to_string(KEEP_ALIVE_TIMEOUT) + "\r\n";
            } else {
                response += "Connection: close\r\n";
            }
            
            response += "\r\n" + error_msg;
            send(client_fd, response.c_str(), response.length(), 0);
            return;
        }

        std::string id_str = body.substr(pos + 7);
        if (id_str.find('&') != std::string::npos) {
            id_str = id_str.substr(0, id_str.find('&'));
        }

        int node_id;
        try {
            node_id = std::stoi(id_str);
        } catch (...) {
            std::string error_msg = "{\"error\":\"Invalid nodeId parameter\"}";
            std::string response = "HTTP/1.1 400 Bad Request\r\n"
                                  "Content-Type: application/json\r\n"
                                  "Content-Length: " + std::to_string(error_msg.length()) + "\r\n";
            
            if (keep_alive) {
                response += "Connection: keep-alive\r\n"
                           "Keep-Alive: timeout=" + std::to_string(KEEP_ALIVE_TIMEOUT) + "\r\n";
            } else {
                response += "Connection: close\r\n";
            }
            
            response += "\r\n" + error_msg;
            send(client_fd, response.c_str(), response.length(), 0);
            return;
        }

        int node_port = 6000 + node_id;
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            std::string error_msg = "{\"error\":\"Failed to create socket\"}";
            std::string response = "HTTP/1.1 500 Internal Server Error\r\n"
                                  "Content-Type: application/json\r\n"
                                  "Content-Length: " + std::to_string(error_msg.length()) + "\r\n";
            
            if (keep_alive) {
                response += "Connection: keep-alive\r\n"
                           "Keep-Alive: timeout=" + std::to_string(KEEP_ALIVE_TIMEOUT) + "\r\n";
            } else {
                response += "Connection: close\r\n";
            }
            
            response += "\r\n" + error_msg;
            send(client_fd, response.c_str(), response.length(), 0);
            return;
        }

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(node_port);
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");

        if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            std::string error_msg = "{\"error\":\"Failed to connect to node\"}";
            std::string response = "HTTP/1.1 500 Internal Server Error\r\n"
                                  "Content-Type: application/json\r\n"
                                  "Content-Length: " + std::to_string(error_msg.length()) + "\r\n";
            
            if (keep_alive) {
                response += "Connection: keep-alive\r\n"
                           "Keep-Alive: timeout=" + std::to_string(KEEP_ALIVE_TIMEOUT) + "\r\n";
            } else {
                response += "Connection: close\r\n";
            }
            
            response += "\r\n" + error_msg;
            send(client_fd, response.c_str(), response.length(), 0);
            close(sock);
            return;
        }

        char buffer[256];
        recv(sock, buffer, sizeof(buffer), 0);

        const char* cmd = "KILL\r\n";
        send(sock, cmd, strlen(cmd), 0);
        
        std::string success = "{\"success\":true}";
        std::string response = "HTTP/1.1 200 OK\r\n"
                              "Content-Type: application/json\r\n"
                              "Content-Length: " + std::to_string(success.length()) + "\r\n";
        
        if (keep_alive) {
            response += "Connection: keep-alive\r\n"
                       "Keep-Alive: timeout=" + std::to_string(KEEP_ALIVE_TIMEOUT) + "\r\n";
        } else {
            response += "Connection: close\r\n";
        }
        
        response += "\r\n" + success;
        
        send(client_fd, response.c_str(), response.length(), 0);
        close(sock);
    }
} 
