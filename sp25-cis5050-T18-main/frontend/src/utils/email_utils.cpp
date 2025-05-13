#include "include/utils.h"
#include <string>
#include <sstream>
#include <iostream>
#include <sys/socket.h>

namespace Utils {

std::string escape_email_content(const std::string& content) {
    std::string result;
    result.reserve(content.size());
    
    for (char c : content) {
        if (c == '\n' || c == '\r') {
            result += "\\n";
        } else if (c == '\\') {
            result += "\\\\";
        } else {
            result += c;
        }
    }
    
    return result;
}

std::string unescape_email_content(const std::string& escaped_content) {
    std::string result;
    result.reserve(escaped_content.size());
    
    for (size_t i = 0; i < escaped_content.size(); ++i) {
        if (escaped_content[i] == '\\' && i + 1 < escaped_content.size()) {
            if (escaped_content[i + 1] == 'n') {
                result += '\n';
                ++i;
            } else if (escaped_content[i + 1] == '\\') {
                result += '\\';
                ++i;
            } else {
                result += escaped_content[i];
            }
        } else {
            result += escaped_content[i];
        }
    }
    
    return result;
}

std::string url_decode(const std::string& encoded) {
    std::string result;
    result.reserve(encoded.length());
    
    for (size_t i = 0; i < encoded.length(); ++i) {
        if (encoded[i] == '%' && i + 2 < encoded.length()) {
            std::string hex = encoded.substr(i + 1, 2);
            int value;
            std::istringstream iss(hex);
            iss >> std::hex >> value;
            
            result += static_cast<char>(value);
            i += 2;
        } else if (encoded[i] == '+') {
            result += ' ';
        } else {
            result += encoded[i];
        }
    }
    
    return result;
}

std::string send_smtp_command(int smtp_fd, const std::string& command) {
    send(smtp_fd, command.c_str(), command.size(), 0);
    std::cout << "SMTP command sent: " << command;
    
    char buffer[1024] = {0};
    int bytes = recv(smtp_fd, buffer, sizeof(buffer) - 1, 0);
    
    if (bytes > 0) {
        buffer[bytes] = '\0';
        std::cout << "SMTP response: " << buffer << std::endl;
        return std::string(buffer);
    }
    
    return "";
}

} 