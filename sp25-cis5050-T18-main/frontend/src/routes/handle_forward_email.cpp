#include "include/routes/routes.h"
#include "include/utils.h"

#include <string>
#include <sstream>
#include <sys/socket.h>
#include <iostream>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

/*
 * Forward Email Handler
 * 
 * Processes email forwarding requests.
 * Implements:
 * - Authentication verification
 * - Email ID and recipient extraction
 * - SMTP protocol communication for email forwarding
 * - Custom FORW command for email redistribution
 * - Error handling for network and authentication issues
 */

// Keep alive timeout in seconds
#define KEEP_ALIVE_TIMEOUT 15

namespace Routes {

void handle_forward_email(int client_fd, const std::string& body, const char* request, bool keep_alive) {
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
    std::string recipients = Utils::get_form_value(body, "recipients");
    
    recipients = Utils::url_decode(recipients);
    
    if (email_id.empty() || recipients.empty()) {
        std::string error_text = "error: Missing required fields";
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
    
    std::cout << "Forwarding email ID " << email_id << " from " << username 
              << " to " << recipients << std::endl;
    
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
    
    std::string forw_command = "FORW " + username + " " + email_id + " " + recipients + "\r\n";
    std::string forw_response = Utils::send_smtp_command(smtp_fd, forw_command);
    
    std::string quit_command = "QUIT\r\n";
    std::string quit_response = Utils::send_smtp_command(smtp_fd, quit_command);
    
    close(smtp_fd);
    
    if (forw_response.find("+OK") != 0 && forw_response.find("250") != 0) {
        std::string error_text = "error: Failed to forward email - " + forw_response;
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
    
    std::string success_text = "success: Email forwarded successfully";
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