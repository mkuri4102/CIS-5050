#include "include/utils.h"
#include <string>
#include <sstream>
#include <iostream>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>

// Keep alive timeout in seconds
#define KEEP_ALIVE_TIMEOUT 15

extern int server_socket;

namespace Utils {

bool parse_http_request(const char* buffer, std::string& method, std::string& path) {
    std::istringstream iss(buffer);
    std::string request_line;
    std::getline(iss, request_line);
    
    size_t method_end = request_line.find(' ');
    if (method_end == std::string::npos) return false;
    
    size_t path_end = request_line.find(' ', method_end + 1);
    if (path_end == std::string::npos) return false;
    
    method = request_line.substr(0, method_end);
    path = request_line.substr(method_end + 1, path_end - method_end - 1);
    
    return true;
}

std::string get_form_value(const std::string& body, const std::string& field) {
    size_t pos = body.find(field + "=");
    if (pos == std::string::npos) return "";
    
    pos += field.length() + 1;
    size_t end_pos = body.find('&', pos);
    if (end_pos == std::string::npos) end_pos = body.length();
    
    return body.substr(pos, end_pos - pos);
}

int get_server_port() {
    struct sockaddr_in sin;
    socklen_t len = sizeof(sin);
    
    if (getsockname(server_socket, (struct sockaddr *)&sin, &len) == -1) {
        perror("getsockname");
        return -1;
    }
    
    return ntohs(sin.sin_port);
}

void redirect_to_service_down_page(int client_fd, bool keep_alive) {
    const char* html_content = 
        "<!DOCTYPE html>\n"
        "<html>\n"
        "<head>\n"
        "    <title>Service Unavailable - PennCloud</title>\n"
        "    <style>\n"
        "        body {\n"
        "            font-family: Arial, sans-serif;\n"
        "            background-color: #f5f5f5;\n"
        "            text-align: center;\n"
        "            margin: 0;\n"
        "            padding: 0;\n"
        "            color: #333;\n"
        "        }\n"
        "        .container {\n"
        "            max-width: 600px;\n"
        "            margin: 100px auto 0;\n"
        "            padding: 30px;\n"
        "            background-color: #fff;\n"
        "            border-radius: 8px;\n"
        "            box-shadow: 0 2px 10px rgba(0, 0, 0, 0.1);\n"
        "        }\n"
        "        h1 {\n"
        "            color: #d32f2f;\n"
        "            margin-bottom: 20px;\n"
        "        }\n"
        "        p {\n"
        "            font-size: 18px;\n"
        "            line-height: 1.6;\n"
        "            margin-bottom: 20px;\n"
        "        }\n"
        "        .home-link {\n"
        "            margin-top: 30px;\n"
        "        }\n"
        "        .home-link a {\n"
        "            display: inline-block;\n"
        "            padding: 10px 20px;\n"
        "            background-color: #4285f4;\n"
        "            color: white;\n"
        "            text-decoration: none;\n"
        "            border-radius: 4px;\n"
        "            font-weight: bold;\n"
        "        }\n"
        "        .home-link a:hover {\n"
        "            background-color: #3367d6;\n"
        "        }\n"
        "    </style>\n"
        "</head>\n"
        "<body>\n"
        "    <div class=\"container\">\n"
        "        <h1>Service Temporarily Unavailable</h1>\n"
        "        <p>We're sorry, but the requested service is currently unavailable due to a system issue.</p>\n"
        "        <p>Our team has been automatically notified and is working to restore service as quickly as possible.</p>\n"
        "        <p>Thank you for your patience.</p>\n"
        "        <div class=\"home-link\">\n"
        "            <a href=\"/\">Return to Homepage</a>\n"
        "        </div>\n"
        "    </div>\n"
        "    \n"
        "    <script>\n"
        "        // Auto-refresh after 10 seconds\n"
        "        setTimeout(function() {\n"
        "            window.location.reload();\n"
        "        }, 10000);\n"
        "    </script>\n"
        "</body>\n"
        "</html>";
    
    size_t content_length = strlen(html_content);
    
    std::string header = "HTTP/1.1 503 Service Unavailable\r\n"
                        "Content-Type: text/html; charset=UTF-8\r\n"
                        "Content-Length: " + std::to_string(content_length) + "\r\n";
    
    if (keep_alive) {
        header += "Connection: keep-alive\r\n"
                 "Keep-Alive: timeout=" + std::to_string(KEEP_ALIVE_TIMEOUT) + "\r\n";
    } else {
        header += "Connection: close\r\n";
    }
    
    header += "\r\n";
    
    // Send header and HTML content
    send(client_fd, header.c_str(), header.size(), 0);
    send(client_fd, html_content, content_length, 0);
    
    fprintf(stderr, "[redirect_to_service_down_page] Sent service-down HTML content directly\n");
}

// Add overload for backward compatibility
void redirect_to_service_down_page(int client_fd) {
    redirect_to_service_down_page(client_fd, false);
}

}