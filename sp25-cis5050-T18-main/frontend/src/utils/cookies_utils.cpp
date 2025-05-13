#include "include/utils.h"
#include <string>
#include <iostream>
#include <cstring>

namespace Utils {

std::string get_cookie_value(const char* headers, const std::string& cookie_name) {
    const char* cookie_header = strstr(headers, "Cookie:");
    if (!cookie_header) {
        cookie_header = strstr(headers, "cookie:");
    }
    
    if (!cookie_header) {
        return "";
    }
    
    cookie_header = strchr(cookie_header, ':');
    if (!cookie_header) {
        return "";
    }
    
    cookie_header++;
    
    const char* end = strstr(cookie_header, "\r\n");
    if (!end) {
        end = cookie_header + strlen(cookie_header);
    }
    
    std::string cookies(cookie_header, end - cookie_header);
    
    std::string search = cookie_name + "=";
    size_t pos = cookies.find(search);
    if (pos == std::string::npos) {
        return "";
    }
    
    pos += search.length();
    size_t end_pos = cookies.find(';', pos);
    
    if (end_pos == std::string::npos) {
        return cookies.substr(pos);
    } else {
        return cookies.substr(pos, end_pos - pos);
    }
}

bool is_authenticated(const char* headers, std::string& username) {
    std::string auth_cookie = get_cookie_value(headers, "auth_user");
    if (auth_cookie.empty()) {
        return false;
    }
    
    username = auth_cookie;
    return true;
}

std::string make_auth_cookie(const std::string& username) {
    return "Set-Cookie: auth_user=" + username + "; Path=/; HttpOnly\r\n";
}

} // namespace Utils 