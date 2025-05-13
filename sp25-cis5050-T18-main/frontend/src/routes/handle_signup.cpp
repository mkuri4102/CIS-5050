#include "include/routes/routes.h"
#include "include/utils.h"

#include <string>
#include <sys/socket.h>
#include <iostream>
#include <iomanip>

/*
 * Signup Handler
 * 
 * Processes user registration requests.
 * Implements:
 * - Form data extraction and validation
 * - Username availability checking
 * - User creation in the key-value store
 * - Error handling for existing usernames and server issues
 * - Appropriate HTTP status codes for different response conditions
 */

// Keep alive timeout in seconds
#define KEEP_ALIVE_TIMEOUT 15

namespace Routes {

void handle_signup(int client_fd, const std::string& body, bool keep_alive) {
    std::string username = Utils::get_form_value(body, "username");
    std::string password = Utils::get_form_value(body, "password");
    
    std::cout << "Signup attempt for user: " << username << std::endl;
    
    if (username.empty() || password.empty()) {
        std::string error_text = "error: Missing credentials";
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
    
    std::string tablet_address = Utils::get_tablet_for_username(username);
    
    if (tablet_address == "SERVICE_DOWN") {
        std::cout << "Service is down for this shard. Sending service down page." << std::endl;
        Utils::redirect_to_service_down_page(client_fd, keep_alive);
        return;
    }
    
    if (tablet_address.empty()) {
        std::string error_text = "error: Internal server error";
        std::string error_response = "HTTP/1.1 500 Internal Server Error\r\n"
                                  "Content-Type: text/plain\r\n"
                                  "Content-Length: " + std::to_string(error_text.size()) + "\r\n";
        
        if (keep_alive) {
            error_response += "Connection: keep-alive\r\n"
                             "Keep-Alive: timeout=" + std::to_string(KEEP_ALIVE_TIMEOUT) + "\r\n";
        } else {
            error_response += "Connection: close\r\n";
        }
        
        error_response += "\r\n" + error_text;
        
        send(client_fd, error_response.c_str(), error_response.size(), 0);
        std::cout << "HTTP Response:\n" << error_response << std::endl;
        return;
    }
    
    std::string kv_command = "GET " + username + " password\r\n";
    auto [kv_response, success] = Utils::tablet_command(tablet_address, kv_command);
    
    if (!success) {
        std::string error_text = "error: Failed to communicate with tablet server";
        std::string error_response = "HTTP/1.1 500 Internal Server Error\r\n"
                                  "Content-Type: text/plain\r\n"
                                  "Content-Length: " + std::to_string(error_text.size()) + "\r\n";
        
        if (keep_alive) {
            error_response += "Connection: keep-alive\r\n"
                             "Keep-Alive: timeout=" + std::to_string(KEEP_ALIVE_TIMEOUT) + "\r\n";
        } else {
            error_response += "Connection: close\r\n";
        }
        
        error_response += "\r\n" + error_text;
        
        send(client_fd, error_response.c_str(), error_response.size(), 0);
        std::cout << "HTTP Response:\n" << error_response << std::endl;
        return;
    }
    
    if (kv_response.find("+OK") == 0) {
        std::string error_text = "error: Username already exists";
        
        std::string error_response = "HTTP/1.1 409 Conflict\r\n"
                                  "Content-Type: text/plain\r\n"
                                  "Content-Length: " + std::to_string(error_text.size()) + "\r\n";
        
        if (keep_alive) {
            error_response += "Connection: keep-alive\r\n"
                             "Keep-Alive: timeout=" + std::to_string(KEEP_ALIVE_TIMEOUT) + "\r\n";
        } else {
            error_response += "Connection: close\r\n";
        }
        
        error_response += "\r\n" + error_text;
        
        send(client_fd, error_response.c_str(), error_response.size(), 0);
        std::cout << "HTTP Response:\n" << error_response << std::endl;
    } else if (kv_response.find("-ERR") == 0) {
        std::string put_command = "PUT " + username + " password " + password + "\r\n";
        auto [put_response, put_success] = Utils::tablet_command(tablet_address, put_command);

        if (!put_success) {
            std::string error_text = "error: Failed to communicate with tablet server";
            std::string error_response = "HTTP/1.1 500 Internal Server Error\r\n"
                                      "Content-Type: text/plain\r\n"
                                      "Content-Length: " + std::to_string(error_text.size()) + "\r\n";
            
            if (keep_alive) {
                error_response += "Connection: keep-alive\r\n"
                                 "Keep-Alive: timeout=" + std::to_string(KEEP_ALIVE_TIMEOUT) + "\r\n";
            } else {
                error_response += "Connection: close\r\n";
            }
            
            error_response += "\r\n" + error_text;
            
            send(client_fd, error_response.c_str(), error_response.size(), 0);
            std::cout << "HTTP Response:\n" << error_response << std::endl;
            return;
        }

        std::string success_text = "success: User created";
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
    } else {
        std::string error_text = "error: Internal server error";
        
        std::string error_response = "HTTP/1.1 500 Internal Server Error\r\n"
                                  "Content-Type: text/plain\r\n"
                                  "Content-Length: " + std::to_string(error_text.size()) + "\r\n";
        
        if (keep_alive) {
            error_response += "Connection: keep-alive\r\n"
                             "Keep-Alive: timeout=" + std::to_string(KEEP_ALIVE_TIMEOUT) + "\r\n";
        } else {
            error_response += "Connection: close\r\n";
        }
        
        error_response += "\r\n" + error_text;
        
        send(client_fd, error_response.c_str(), error_response.size(), 0);
        std::cout << "HTTP Response:\n" << error_response << std::endl;
    }
}
} 
