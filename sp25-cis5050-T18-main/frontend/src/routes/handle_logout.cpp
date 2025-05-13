#include "include/routes/routes.h"
#include "include/utils.h"

#include <string>
#include <sys/socket.h>
#include <iostream>

/*
 * Logout Handler
 * 
 * Processes user logout requests.
 * Implements:
 * - Authentication verification
 * - Cookie clearing by setting an expired timestamp
 * - User session termination
 * - Keep-alive connection management
 */

// Keep alive timeout in seconds
#define KEEP_ALIVE_TIMEOUT 15

namespace Routes {

void handle_logout(int client_fd, const char* buffer, bool keep_alive) {
    std::string username;
    bool is_logged_in = Utils::is_authenticated(buffer, username);

    std::cout << "Logout requested. User authenticated: " << (is_logged_in ? "yes" : "no") << std::endl;
    if (is_logged_in) {
        std::cout << "Logging out user: " << username << std::endl;
    }

    std::string logout_text = "success: Logged out";
    
    std::string clear_cookie = "Set-Cookie: auth_user=; Path=/; Expires=Thu, 01 Jan 1970 00:00:00 GMT; HttpOnly\r\n";
    
    std::string logout_response = "HTTP/1.1 200 OK\r\n"
                               "Content-Type: text/plain\r\n"
                               + clear_cookie +
                               "Content-Length: " + std::to_string(logout_text.size()) + "\r\n";
    
    if (keep_alive) {
        logout_response += "Connection: keep-alive\r\n"
                         "Keep-Alive: timeout=" + std::to_string(KEEP_ALIVE_TIMEOUT) + "\r\n";
    } else {
        logout_response += "Connection: close\r\n";
    }
    
    logout_response += "\r\n" + logout_text;
    
    send(client_fd, logout_response.c_str(), logout_response.size(), 0);
    std::cout << "HTTP Response:\n" << logout_response << std::endl;
}

} 