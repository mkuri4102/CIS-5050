#ifndef HANDLERS_H
#define HANDLERS_H

#include <string>

namespace Routes {
    
    // handle a GET request
    void handle_get_request(int client_fd, const std::string& path, const char* buffer, bool keep_alive = false);
    
    // handle a POST request
    void handle_post_request(int client_fd, const std::string& path, const char* buffer, bool keep_alive = false);
    
    // handle a HEAD request
    void handle_head_request(int client_fd, const std::string& path, const char* buffer, bool keep_alive = false);
}

#endif

