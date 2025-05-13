#include "include/routes/routes.h"
#include "include/utils.h"
#include <sstream>
#include <unistd.h>
#include <sys/socket.h>

/*
 * Server ID Handler
 * 
 * Provides information about the current server instance.
 * Implements:
 * - Hostname retrieval
 * - Process ID (PID) reporting
 * - Server port information
 * - Keep-alive connection management
 */

// Keep alive timeout in seconds
#define KEEP_ALIVE_TIMEOUT 15

namespace Routes {
    void handle_server_id(int client_fd, bool keep_alive) {
        char hostname[256];
        gethostname(hostname, sizeof(hostname));
        
        std::ostringstream response_stream;
        response_stream << "HTTP/1.1 200 OK\r\n"
                      << "Content-Type: text/plain\r\n";
        
        if (keep_alive) {
            response_stream << "Connection: keep-alive\r\n"
                          << "Keep-Alive: timeout=" << KEEP_ALIVE_TIMEOUT << "\r\n";
        } else {
            response_stream << "Connection: close\r\n";
        }
        
        std::string server_info = "Server ID Information:\n"
                                  "Hostname: " + std::string(hostname) + "\n"
                                  "PID: " + std::to_string(getpid()) + "\n"
                                  "Port: " + std::to_string(Utils::get_server_port()) + "\n";
        
        response_stream << "Content-Length: " << server_info.length() << "\r\n"
                      << "\r\n"
                      << server_info;
        
        std::string response = response_stream.str();
        send(client_fd, response.c_str(), response.length(), 0);
    }
} 