#include "include/handlers/handlers.h"
#include "include/routes/routes.h"
#include "include/utils.h"
#include "include/webstorage/webstorage_handler.hpp"

#include <unistd.h>
#include <string>
#include <sys/socket.h>
#include <iostream>

/*
 * POST Request Handler
 * 
 * Processes HTTP POST requests for the web server.
 * Handles various routes including:
 * - Authentication operations (login, signup, logout, reset-password)
 * - Email operations (send, forward, reply, delete)
 * - WebStorage requests (/storage/*)
 * - Admin operations (kill-node, restart-node)
 * - Keep-alive connection management
 */

// Keep alive timeout in seconds
#define KEEP_ALIVE_TIMEOUT 15

namespace Routes {

void handle_post_request(int client_fd, const std::string& path, const char* buffer, bool keep_alive) {
    std::cout << "Received POST request to: " << path << std::endl;
    
    if (path.find("/storage/") == 0) {
        WebStorageHandler::handle_storage_request(client_fd, path, buffer);
        return;
    }
    
    std::string request(buffer);
    size_t body_start = request.find("\r\n\r\n");
    std::string body = "";
    
    if (body_start != std::string::npos) {
        body = request.substr(body_start + 4);
    }
    
    if (path == "/api/login") {
        handle_login(client_fd, body, keep_alive);
    } else if (path == "/api/signup") {
        handle_signup(client_fd, body, keep_alive);
    } else if (path == "/api/reset-password") {
        handle_reset_password(client_fd, body, keep_alive);
    } else if (path == "/api/logout") {
        handle_logout(client_fd, buffer, keep_alive);
    } else if (path == "/api/send-email") {
        handle_send_email(client_fd, body, buffer, keep_alive);
    } else if (path == "/api/forward-email") {
        handle_forward_email(client_fd, body, buffer, keep_alive);
    } else if (path == "/api/reply-email") {
        handle_reply_email(client_fd, body, buffer, keep_alive);
    } else if (path == "/api/delete-email") {
        handle_delete_email(client_fd, body, buffer, keep_alive);
    } else if (path == "/api/admin/kill-node") {
        handle_kill_node(client_fd, body, keep_alive);
    } else if (path == "/api/admin/restart-node") {
        handle_restart_node(client_fd, body, keep_alive);
    } else {
        std::string response = "HTTP/1.1 404 Not Found\r\n"
                             "Content-Type: text/plain\r\n"
                             "Content-Length: 15\r\n";
        if (keep_alive) {
            response += "Connection: keep-alive\r\n"
                        "Keep-Alive: timeout=" + std::to_string(KEEP_ALIVE_TIMEOUT) + "\r\n";
        } else {
            response += "Connection: close\r\n";
        }
        response += "\r\n"
                    "error: Not found";
        
        send(client_fd, response.c_str(), response.size(), 0);
        std::cout << "HTTP Response:\n" << response << std::endl;
    }
}
} 
