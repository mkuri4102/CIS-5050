#include "include/routes/routes.h"
#include "include/utils.h"
#include <string>
#include <sys/socket.h>
#include <iostream>
#include <sstream>
#include <vector>

/*
 * Get Emails Handler
 * 
 * Retrieves and formats email messages for authenticated users.
 * Implements:
 * - Authentication verification
 * - Email ID list retrieval from key-value store
 * - Individual email content retrieval
 * - Email formatting with metadata
 * - Content escaping for safe transmission
 * - Error handling for network and authentication issues
 */

// Keep alive timeout in seconds
#define KEEP_ALIVE_TIMEOUT 15

namespace Routes {

void handle_get_emails(int client_fd, const std::string& request_headers, bool keep_alive) {
    std::string username;
    bool is_authenticated = Utils::is_authenticated(request_headers.c_str(), username);
    
    if (!is_authenticated || username.empty()) {
        std::string error_text = "error: Unauthorized";
        std::string error_response = "HTTP/1.1 401 Unauthorized\r\n"
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
    
    std::cout << "Getting emails for user: " << username << std::endl;
    
    std::string tablet_address = Utils::get_tablet_for_username(username);
    
    if (tablet_address == "SERVICE_DOWN") {
        std::cout << "Service is down for this shard. Sending service down page." << std::endl;
        Utils::redirect_to_service_down_page(client_fd, keep_alive);
        return;
    }
    
    if (tablet_address.empty()) {
        std::string error_text = "error: Internal server error - Cannot determine tablet server";
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
    
    std::string get_emails_cmd = "GET " + username + " emails\r\n";
    std::cout << "Sending command to get email count: " << get_emails_cmd;
    auto [emails_response, emails_success] = Utils::tablet_command(tablet_address, get_emails_cmd);
    
    std::vector<int> email_ids;
    
    if (!emails_success) {
        std::string error_text = "error: Failed to retrieve email list";
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
    
    if (emails_response.find("+OK") == 0) {
        std::string email_list_str = emails_response.substr(4);
        if (email_list_str.length() >= 2 && 
            email_list_str.substr(email_list_str.length() - 2) == "\r\n") {
            email_list_str = email_list_str.substr(0, email_list_str.length() - 2);
        }
        
        std::cout << "Raw email list response: '" << emails_response << "'" << std::endl;
        std::cout << "Extracted email IDs string: '" << email_list_str << "'" << std::endl;
        
        std::istringstream iss(email_list_str);
        int id;
        while (iss >> id) {
            email_ids.push_back(id);
        }
    } else if (emails_response.find("-ERR") == 0) {
        std::cout << "User has no emails yet (no 'emails' key found)" << std::endl;
    } else {
        std::string error_text = "error: Unexpected response from server";
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
    
    std::cout << "Found " << email_ids.size() << " email IDs" << std::endl;
    
    std::vector<std::string> retrieved_emails;
    
    for (const auto& email_id_num : email_ids) {
        std::string email_id = "EMAIL" + std::to_string(email_id_num);
        std::string get_email_cmd = "GET " + username + " " + email_id + "\r\n";
        
        std::cout << "Sending tablet command: " << get_email_cmd;
        auto [email_response, email_success] = Utils::tablet_command(tablet_address, get_email_cmd);
        
        if (!email_success) {
            std::cout << "Network failure when retrieving email " << email_id << std::endl;
            continue;
        }
        
        if (email_response.find("+OK") != 0) {
            std::cout << "Failed to retrieve email " << email_id << ": " << email_response << std::endl;
            continue;
        }
        
        std::string email_content = email_response.substr(4);
        if (email_content.length() >= 2 && 
            email_content.substr(email_content.length() - 2) == "\r\n") {
            email_content = email_content.substr(0, email_content.length() - 2);
        }
        
        std::cout << "Successfully retrieved email " << email_id << ", content length: " << email_content.length() << std::endl;
        
        std::string escaped_content = Utils::escape_email_content(email_content);
        
        std::stringstream email_entry;
        email_entry << "-----EMAIL_START-----\n";
        email_entry << "ID: " << email_id_num << "\n";
        email_entry << "CONTENT: " << escaped_content << "\n";
        email_entry << "READ: 0\n";
        email_entry << "-----EMAIL_END-----\n";
        
        retrieved_emails.push_back(email_entry.str());
    }
    
    std::stringstream response_body;
    response_body << retrieved_emails.size() << "\n";
    
    for (const auto& email : retrieved_emails) {
        response_body << email;
    }
    
    std::string response_text = response_body.str();
    
    std::string response = "HTTP/1.1 200 OK\r\n"
                       "Content-Type: text/plain\r\n"
                       "Content-Length: " + std::to_string(response_text.size()) + "\r\n";
    
    if (keep_alive) {
        response += "Connection: keep-alive\r\n"
                    "Keep-Alive: timeout=" + std::to_string(KEEP_ALIVE_TIMEOUT) + "\r\n";
    } else {
        response += "Connection: close\r\n";
    }
    
    response += "\r\n" + response_text;
    
    send(client_fd, response.c_str(), response.size(), 0);
    std::cout << "Sent " << retrieved_emails.size() << " emails to client" << std::endl;
}

} 