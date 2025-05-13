#ifndef UTILS_H
#define UTILS_H

#include <string>
#include <map>

namespace Utils {
    // parse an http request
    bool parse_http_request(const char* buffer, std::string& method, std::string& path);


    bool connect_to_kvstore();
    
    // send a command to the kvstore
    std::string kvstore_command(const std::string& command);

    // get a form value from a request body
    std::string get_form_value(const std::string& body, const std::string& field);
    
    // get the tablet for a username
    std::string get_tablet_for_username(const std::string& username);
    
    // send a command to a tablet
    std::pair<std::string, bool> tablet_command(const std::string& tablet_address, const std::string& command);
    
    // get a cookie value from a request headers
    std::string get_cookie_value(const char* headers, const std::string& cookie_name);
    
    // check if a user is authenticated
    bool is_authenticated(const char* headers, std::string& username);
    
    // make an auth cookie
    std::string make_auth_cookie(const std::string& username);
    
    // escape email content
    std::string escape_email_content(const std::string& content);
    
    // unescape email content
    std::string unescape_email_content(const std::string& escaped_content);
    
    // url decode a string
    std::string url_decode(const std::string& encoded);
    
    // send an smtp command
    std::string send_smtp_command(int smtp_fd, const std::string& command);
    
    // get the server port
    int get_server_port();
    
    // redirect to the service down page
    void redirect_to_service_down_page(int client_fd);
    
    // redirect to the service down page with keep alive
    void redirect_to_service_down_page(int client_fd, bool keep_alive);
}

#endif
