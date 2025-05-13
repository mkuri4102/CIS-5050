#include "include/handlers/handlers.h"
#include "include/handlers/handler_utils.h"
#include "include/utils.h"
#include "include/routes/routes.h"
#include "include/webstorage/webstorage_handler.hpp"

#include <fstream>
#include <sstream>
#include <iostream>
#include <unistd.h>
#include <sys/socket.h>
#include <vector>

/*
 * GET Request Handler
 * 
 * Processes HTTP GET requests for the web server.
 * Handles various routes including:
 * - Static file serving from the public directory
 * - API endpoints (/api/emails, /api/admin/*)
 * - WebStorage requests (/storage/*)
 * - Protected routes with authentication
 * - Keep-alive connection management
 */

// Keep alive timeout in seconds
#define KEEP_ALIVE_TIMEOUT 15

namespace Routes {

void handle_get_request(int client_fd, const std::string& path, const char* request, bool keep_alive) {
    std::string clean_path = path;
    
    size_t question_mark = path.find('?');
    if (question_mark != std::string::npos) {
        clean_path = path.substr(0, question_mark);
    }
    
    if (clean_path.find("/storage/") == 0) {
        WebStorageHandler::handle_storage_request(client_fd, clean_path, request);
        return;
    }
    
    if (clean_path == "/server-id") {
        handle_server_id(client_fd, keep_alive);
        return;
    }
    
    if (clean_path == "/api/emails") {
        handle_get_emails(client_fd, request, keep_alive);
        return;
    }
    
    if (clean_path == "/api/admin/rows") {
        handle_get_db_rows(client_fd, keep_alive);
        return;
    }
    
    if (clean_path == "/api/admin/columns") {
        handle_get_db_columns(client_fd, path, keep_alive);
        return;
    }
    
    if (clean_path == "/api/admin/value") {
        handle_get_db_value(client_fd, path, keep_alive);
        return;
    }
    
    if (clean_path == "/api/admin/nodes") {
        handle_admin_nodes(client_fd, keep_alive);
        return;
    }
    
    if (clean_path == "/api/admin/node-data") {
        handle_admin_node_data(client_fd, path, keep_alive);
        return;
    }
    
    if (is_protected_route(clean_path)) {
        std::string username;
        if (!Utils::is_authenticated(request, username)) {
            std::string redirect_response = "HTTP/1.1 302 Found\r\n"
                                         "Location: /login\r\n"
                                         "Content-Length: 0\r\n";
            if (keep_alive) {
                redirect_response += "Connection: keep-alive\r\n"
                                    "Keep-Alive: timeout=" + std::to_string(KEEP_ALIVE_TIMEOUT) + "\r\n";
            } else {
                redirect_response += "Connection: close\r\n";
            }
            redirect_response += "\r\n";
            
            send(client_fd, redirect_response.c_str(), redirect_response.size(), 0);
            std::cout << "Unauthorized access to " << clean_path << ", redirecting to login" << std::endl;
            return;
        }
        
        std::cout << "Authenticated user " << username << " accessing " << clean_path << std::endl;
    }
    
    if (clean_path.rfind("/api/", 0) == 0) {
        std::string not_found_response = "HTTP/1.1 404 Not Found\r\n"
                                        "Content-Type: application/json\r\n"
                                        "Content-Length: 30\r\n";
        if (keep_alive) {
            not_found_response += "Connection: keep-alive\r\n"
                                 "Keep-Alive: timeout=" + std::to_string(KEEP_ALIVE_TIMEOUT) + "\r\n";
        } else {
            not_found_response += "Connection: close\r\n";
        }
        not_found_response += "\r\n"
                             "{\"error\": \"API endpoint not found\"}";
        send(client_fd, not_found_response.c_str(), not_found_response.size(), 0);
        std::cout << "API endpoint not found: " << clean_path << std::endl;
        return;
    }
    
    std::string file_path = "public" + clean_path;
    
    if (clean_path == "/") file_path = "public/index.html";
    if (clean_path == "/storage" || clean_path == "/storage/") file_path = "public/storage.html";
    if (clean_path == "/webmail" || clean_path == "/webmail/") file_path = "public/webmail.html";
    if (clean_path == "/admin/data" || clean_path == "/admin/data/") file_path = "public/admin/data.html";
    
    if (!ends_with(clean_path, ".html") && 
        !ends_with(clean_path, ".css") && 
        !clean_path.empty() && 
        clean_path != "/" &&
        clean_path != "/storage" && 
        clean_path != "/storage/" && 
        clean_path != "/webmail" && 
        clean_path != "/webmail/") {
        file_path = "public" + clean_path + ".html";
    }
    
    std::string content_type = "text/html";
    if (ends_with(clean_path, ".css")) {
        content_type = "text/css";
    }
    
    std::ifstream file(file_path, std::ios::binary);
    if (file.is_open()) {
        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();
        
        if (is_protected_route(clean_path) && content_type == "text/html") {
            std::string username;
            Utils::is_authenticated(request, username);
            
            size_t pos = content.find("{{username}}");
            if (pos != std::string::npos) {
                content.replace(pos, 12, username);
            }
        }
        
        std::cout << "Serving file: " << file_path << " with content type: " << content_type << std::endl;
        
        std::ostringstream response;
        response << "HTTP/1.1 200 OK\r\n"
                 << "Content-Type: " << content_type << "\r\n"
                 << "Content-Length: " << content.size() << "\r\n";

        if (keep_alive) {
            response << "Connection: keep-alive\r\n"
                     << "Keep-Alive: timeout=" << KEEP_ALIVE_TIMEOUT << "\r\n";
        } else {
            response << "Connection: close\r\n";
        }
        
        response << "\r\n" << content;
        
        std::string response_str = response.str();
        send(client_fd, response_str.c_str(), response_str.size(), 0);
        
        std::string headers = response_str.substr(0, response_str.find("\r\n\r\n") + 4);
        std::cout << "HTTP Response Headers:\n" << headers << "[Content not shown, length: " 
                  << content.size() << " bytes]" << std::endl;
    } else {
        std::cout << "File not found: " << file_path << std::endl;
        std::string not_found_response = "HTTP/1.1 404 Not Found\r\n"
                                        "Content-Type: text/html\r\n"
                                        "Content-Length: 103\r\n";
        if (keep_alive) {
            not_found_response += "Connection: keep-alive\r\n"
                                 "Keep-Alive: timeout=" + std::to_string(KEEP_ALIVE_TIMEOUT) + "\r\n";
        } else {
            not_found_response += "Connection: close\r\n";
        }
        not_found_response += "\r\n"
                             "<html><body><h1>404 Not Found</h1><p>The requested URL was not found on this server.</p></body></html>";
        send(client_fd, not_found_response.c_str(), not_found_response.size(), 0);
        std::cout << "HTTP Response:\n" << not_found_response << std::endl;
    }
}

}
