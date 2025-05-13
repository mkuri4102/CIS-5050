#include "include/routes/routes.h"
#include "include/utils.h"

#include <string>
#include <sys/socket.h>
#include <iostream>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

/*
 * Delete Email Handler
 * 
 * Processes email deletion requests.
 * Implements:
 * - Authentication verification
 * - Email ID extraction and validation
 * - SMTP protocol communication for email deletion
 * - Custom DELE command for email removal
 * - Error handling for network and authentication issues
 */

// Keep alive timeout in seconds
#define KEEP_ALIVE_TIMEOUT 15

namespace Routes {

void handle_delete_email(int client_fd, const std::string& body, const char* request, bool keep_alive) {
    std::string username;
    if (!Utils::is_authenticated(request, username)) {
        std::string error_text = "error: Not authenticated";
        std::string response = "HTTP/1.1 401 Unauthorized\r\n"
                               "Content-Type: text/plain\r\n"
                               "Content-Length: " + std::to_string(error_text.size()) + "\r\n";
        
        if (keep_alive) {
            response += "Connection: keep-alive\r\n"
                        "Keep-Alive: timeout=" + std::to_string(KEEP_ALIVE_TIMEOUT) + "\r\n";
        } else {
            response += "Connection: close\r\n";
        }
        
        response += "\r\n" + error_text;
        
        send(client_fd, response.c_str(), response.size(), 0);
        std::cout << "HTTP Response:\n" << response << std::endl;
        return;
    }
    
    std::string email_id = Utils::get_form_value(body, "email_id");
    
    if (email_id.empty()) {
        std::string error_text = "error: Missing email ID";
        std::string response = "HTTP/1.1 400 Bad Request\r\n"
                               "Content-Type: text/plain\r\n"
                               "Content-Length: " + std::to_string(error_text.size()) + "\r\n";
        
        if (keep_alive) {
            response += "Connection: keep-alive\r\n"
                        "Keep-Alive: timeout=" + std::to_string(KEEP_ALIVE_TIMEOUT) + "\r\n";
        } else {
            response += "Connection: close\r\n";
        }
        
        response += "\r\n" + error_text;
        
        send(client_fd, response.c_str(), response.size(), 0);
        std::cout << "HTTP Response:\n" << response << std::endl;
        return;
    }
    
    std::cout << "Deleting email ID " << email_id << " for user " << username << std::endl;
    
    int smtp_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (smtp_fd < 0) {
        std::string error_text = "error: Failed to create socket for SMTP";
        std::string response = "HTTP/1.1 500 Internal Server Error\r\n"
                               "Content-Type: text/plain\r\n"
                               "Content-Length: " + std::to_string(error_text.size()) + "\r\n";
        
        if (keep_alive) {
            response += "Connection: keep-alive\r\n"
                        "Keep-Alive: timeout=" + std::to_string(KEEP_ALIVE_TIMEOUT) + "\r\n";
        } else {
            response += "Connection: close\r\n";
        }
        
        response += "\r\n" + error_text;
        
        send(client_fd, response.c_str(), response.size(), 0);
        std::cout << "HTTP Response:\n" << response << std::endl;
        return;
    }
    
    struct sockaddr_in smtp_addr;
    smtp_addr.sin_family = AF_INET;
    smtp_addr.sin_port = htons(2500);
    smtp_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    
    if (connect(smtp_fd, (struct sockaddr*)&smtp_addr, sizeof(smtp_addr)) < 0) {
        close(smtp_fd);
        std::cout << "SMTP service is down. Sending service down page." << std::endl;
        Utils::redirect_to_service_down_page(client_fd, keep_alive);
        return;
    }
    
    char buffer[1024] = {0};
    read(smtp_fd, buffer, 1024);
    std::cout << "SMTP server greeting: " << buffer << std::endl;
    
    
    std::string dele_command = "DELE " + username + " " + email_id + "\r\n";
    std::string dele_response = Utils::send_smtp_command(smtp_fd, dele_command);
    
    
    std::string quit_command = "QUIT\r\n";
    std::string quit_response = Utils::send_smtp_command(smtp_fd, quit_command);
    
    close(smtp_fd);
    
    if (dele_response.find("+OK") != 0 && dele_response.find("250") != 0) {
        std::string error_text = "error: Failed to delete email - " + dele_response;
        std::string response = "HTTP/1.1 500 Internal Server Error\r\n"
                               "Content-Type: text/plain\r\n"
                               "Content-Length: " + std::to_string(error_text.size()) + "\r\n";
        
        if (keep_alive) {
            response += "Connection: keep-alive\r\n"
                        "Keep-Alive: timeout=" + std::to_string(KEEP_ALIVE_TIMEOUT) + "\r\n";
        } else {
            response += "Connection: close\r\n";
        }
        
        response += "\r\n" + error_text;
        
        send(client_fd, response.c_str(), response.size(), 0);
        std::cout << "HTTP Response:\n" << response << std::endl;
        return;
    }
    
    std::string success_text = "success: Email deleted successfully";
    std::string success_response = "HTTP/1.1 200 OK\r\n"
                                   "Content-Type: text/plain\r\n"
                                   "Content-Length: " + std::to_string(success_text.size()) + "\r\n";
    
    if (keep_alive) {
        success_response += "Connection: keep-alive\r\n"
                            "Keep-Alive: timeout=" + std::to_string(KEEP_ALIVE_TIMEOUT) + "\r\n";
    } else {
        success_response += "Connection: close\r\n";
    }
    
    success_response += "\r\n" + success_text;
    
    send(client_fd, success_response.c_str(), success_response.size(), 0);
    std::cout << "HTTP Response:\n" << success_response << std::endl;
}

} 