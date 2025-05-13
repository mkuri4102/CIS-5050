#ifndef ROUTES_H
#define ROUTES_H

#include <string>

namespace Routes {

    void handle_login(int client_fd, const std::string& body, bool keep_alive = false);
    
    void handle_signup(int client_fd, const std::string& body, bool keep_alive = false);
    
    void handle_reset_password(int client_fd, const std::string& body, bool keep_alive = false);
    
    void handle_logout(int client_fd, const char* buffer, bool keep_alive = false);
    
    void handle_get_emails(int client_fd, const std::string& request_headers, bool keep_alive = false);
    
    void handle_send_email(int client_fd, const std::string& body, const char* request, bool keep_alive = false);
    
    void handle_forward_email(int client_fd, const std::string& body, const char* request, bool keep_alive = false);
    
    void handle_reply_email(int client_fd, const std::string& body, const char* request, bool keep_alive = false);
    
    void handle_delete_email(int client_fd, const std::string& body, const char* request, bool keep_alive = false);
    
    void handle_server_id(int client_fd, bool keep_alive = false);
    
    // New admin database functions
    void handle_get_db_rows(int client_fd, bool keep_alive = false);
    
    void handle_get_db_columns(int client_fd, const std::string& path, bool keep_alive = false);
    
    void handle_get_db_value(int client_fd, const std::string& path, bool keep_alive = false);
    
    // Admin nodes function
    void handle_admin_nodes(int client_fd, bool keep_alive = false);
    
    // Data for specific node
    void handle_admin_node_data(int client_fd, const std::string& path, bool keep_alive = false);
    
    // Node control functions
    void handle_kill_node(int client_fd, const std::string& body, bool keep_alive = false);
    
    void handle_restart_node(int client_fd, const std::string& body, bool keep_alive = false);
}

#endif