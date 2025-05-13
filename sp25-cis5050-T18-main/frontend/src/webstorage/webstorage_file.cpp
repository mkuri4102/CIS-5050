#include "include/webstorage/webstorage_file.hpp"
#include <ctime>
#include <sstream>

namespace File {
std::string timestamp() {
    time_t now = time(0);
    tm* gmtm = gmtime(&now);
    char buffer[80];
    strftime(buffer, sizeof(buffer), "[%Y-%m-%d-%H-%M-%S]", gmtm);
    return std::string(buffer);
}

std::string Metadata::to_string() const {
    std::stringstream ss;
    ss << "type:" << type << ";part:" << part << ";total:" << total << ";timestamp:" << timestamp;
    return ss.str();
}

Metadata Metadata::from_string(const std::string& str) {
    Metadata metadata;
    size_t pos = 0;
    while (pos < str.length()) {
        size_t colon = str.find(':', pos);
        if (colon == std::string::npos) break;
        std::string key = str.substr(pos, colon - pos);
        pos = colon + 1;
        size_t semicolon = str.find(';', pos);
        if (semicolon == std::string::npos) semicolon = str.length();
        std::string value = str.substr(pos, semicolon - pos);
        pos = semicolon + 1;
        
        if (key == "type") metadata.type = value;
        else if (key == "part") metadata.part = std::stoi(value);
        else if (key == "total") metadata.total = std::stoi(value);
        else if (key == "timestamp") metadata.timestamp = value;
    }
    return metadata;
}

std::string create_file_metadata(const int& part, const int& total, const std::string& content_type) {
    Metadata metadata;
    metadata.type = content_type;
    metadata.part = part;
    metadata.total = total;
    metadata.timestamp = timestamp();
    return metadata.to_string();
}

std::string create_folder_metadata() {
    Metadata metadata;
    metadata.type = "folder";
    metadata.part = 1;
    metadata.total = 1;
    metadata.timestamp = timestamp();
    return metadata.to_string();
}

std::string normalize_path(const std::string& path) {
    std::string normalized_path;
    bool last_was_slash = false;
    for (char c : path) {
        if (c == '/') {
            if (!last_was_slash) {
                normalized_path += c;
                last_was_slash = true;
            }
        } else {
            normalized_path += c;
            last_was_slash = false;
        }
    }
    
    if (!normalized_path.empty() && normalized_path[0] != '/') {
        normalized_path = "/" + normalized_path;
    }
    
    return normalized_path.empty() ? "/" : normalized_path;
}

std::string get_file_key(const std::string& current_path, const std::string& filename) {
    if (current_path == "/") {
        return "/" + filename;
    }
    return current_path + (current_path.back() == '/' ? "" : "/") + filename;
}

std::string get_folder_key(const std::string& current_path, const std::string& foldername) {
    if (current_path == "/") {
        return "/" + foldername;
    }
    return current_path + (current_path.back() == '/' ? "" : "/") + foldername;
}
}

