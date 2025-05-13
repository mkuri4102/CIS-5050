#include "include/webstorage/webstorage_handler.hpp"
#include "include/webstorage/webstorage.hpp"
#include "include/utils.h"
#include <sstream>
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>
#include <sys/socket.h>
#include <fstream>
#include <filesystem>
#include <set>
#include <thread>
#include <chrono>
#include <fcntl.h>
#include <cstddef>
#include <algorithm>

#define KEEP_ALIVE_TIMEOUT 15

namespace WebStorageHandler {

namespace {
    std::string decode_url(const std::string& encoded) {
        std::string decoded;
        for (size_t i = 0; i < encoded.length(); i++) {
            if (encoded[i] == '%' && i + 2 < encoded.length()) {
                int hex = std::stoi(encoded.substr(i + 1, 2), nullptr, 16);
                decoded += static_cast<char>(hex);
                i += 2;
            } else if (encoded[i] == '+') {
                decoded += ' ';
            } else {
                decoded += encoded[i];
            }
        }
        return decoded;
    }

    void send_response(int client_fd, const std::string& status, const std::string& content_type, 
                      const std::string& body, const std::string& additional_headers = "", bool keep_alive = false) {
        std::string response = "HTTP/1.1 " + status + "\r\n"
                             "Content-Type: " + content_type + "\r\n"
                             "Content-Length: " + std::to_string(body.length()) + "\r\n";
                           
	    if (keep_alive) {
            response += "Connection: keep-alive\r\n"
                        "Keep-Alive: timeout=" + std::to_string(KEEP_ALIVE_TIMEOUT) + "\r\n";
        } else {
            response += "Connection: close\r\n";
        }
        
        if (!additional_headers.empty()) {
            response += additional_headers;
        }
        response += "\r\n" + body;
        send(client_fd, response.c_str(), response.length(), 0);
    }

    void send_json_response(int client_fd, bool success, const std::string& message, bool keep_alive = false) {
        std::string json = "{\"success\":" + std::string(success ? "true" : "false") + 
                          ",\"message\":\"" + message + "\"}";
        send_response(client_fd, success ? "200 OK" : "400 Bad Request", 
                     "application/json", json, "", keep_alive);
    }

    void send_file_response(int client_fd, const std::string& file_path, bool keep_alive = false) {
        std::ifstream file(file_path);
        if (file.is_open()) {
            std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            send_response(client_fd, "200 OK", "text/html", content, "", keep_alive);
        } else {
            send_response(client_fd, "404 Not Found", "text/html", 
                         "<html><body><h1>404 Not Found</h1></body></html>", "", keep_alive);
        }
    }

    bool find_boundary(const std::vector<std::byte>& data, const std::string& boundary, size_t start_pos, size_t& found_pos, bool& is_final) {
        std::string boundary_marker = "--" + boundary;
        std::string final_boundary = "--" + boundary + "--";
        std::vector<std::byte> boundary_bytes;
        std::vector<std::byte> final_boundary_bytes;
        
        boundary_bytes.reserve(boundary_marker.size());
        final_boundary_bytes.reserve(final_boundary.size());
        
        for (char c : boundary_marker) {
            boundary_bytes.push_back(static_cast<std::byte>(c));
        }
        for (char c : final_boundary) {
            final_boundary_bytes.push_back(static_cast<std::byte>(c));
        }
        
        for (size_t i = start_pos; i < data.size() - std::min(boundary_bytes.size(), final_boundary_bytes.size()); ++i) {
            bool match = true;
            bool is_final_match = true;
            
            for (size_t j = 0; j < boundary_bytes.size(); ++j) {
                if (data[i + j] != boundary_bytes[j]) {
                    match = false;
                    break;
                }
            }
            
            if (match && i + final_boundary_bytes.size() <= data.size()) {
                for (size_t j = 0; j < final_boundary_bytes.size(); ++j) {
                    if (data[i + j] != final_boundary_bytes[j]) {
                        is_final_match = false;
                        break;
                    }
                }
                if (is_final_match) {
                    found_pos = i;
                    is_final = true;
                    return true;
                }
            }
            
            if (match) {
                found_pos = i;
                is_final = false;
                return true;
            }
        }
        return false;
    }

    bool find_headers_end(const std::vector<std::byte>& data, size_t start_pos, size_t& end_pos) {
        std::vector<std::byte> marker = {static_cast<std::byte>('\r'), static_cast<std::byte>('\n'), 
                                       static_cast<std::byte>('\r'), static_cast<std::byte>('\n')};
        
        for (size_t i = start_pos; i < data.size() - marker.size(); ++i) {
            bool match = true;
            for (size_t j = 0; j < marker.size(); ++j) {
                if (data[i + j] != marker[j]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                end_pos = i + marker.size();
                return true;
            }
        }
        return false;
    }

    std::string extract_header_value(const std::vector<std::byte>& data, const std::string& header_name) {
        std::string header_start = header_name + "=\"";
        std::vector<std::byte> header_bytes;
        header_bytes.reserve(header_start.size());
        for (char c : header_start) {
            header_bytes.push_back(static_cast<std::byte>(c));
        }
        
        for (size_t i = 0; i < data.size() - header_bytes.size(); ++i) {
            bool match = true;
            for (size_t j = 0; j < header_bytes.size(); ++j) {
                if (data[i + j] != header_bytes[j]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                size_t start = i + header_bytes.size();
                size_t end = start;
                while (end < data.size() && data[end] != static_cast<std::byte>('"')) {
                    ++end;
                }
                if (end > start) {
                    std::string result;
                    result.reserve(end - start);
                    for (size_t j = start; j < end; ++j) {
                        result += static_cast<char>(data[j]);
                    }
                    return result;
                }
            }
        }
        return "";
    }

    bool is_text_file(const std::string& filename) {
        static const std::set<std::string> text_extensions = {
            ".txt", ".csv", ".json", ".xml", ".html", ".htm", ".css", ".js",
            ".md", ".log", ".ini", ".conf", ".config", ".yaml", ".yml",
            ".sh", ".bat", ".cmd", ".ps1", ".py", ".java", ".cpp", ".h",
            ".hpp", ".c", ".h", ".cs", ".php", ".rb", ".pl", ".sql"
        };
        
        size_t dot_pos = filename.find_last_of('.');
        if (dot_pos == std::string::npos) return false;
        
        std::string ext = filename.substr(dot_pos);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        return text_extensions.find(ext) != text_extensions.end();
    }

    std::vector<char> convert_to_char_vector(const std::vector<std::byte>& data) {
        std::vector<char> result;
        result.reserve(data.size());
        for (std::byte b : data) {
            result.push_back(static_cast<char>(b));
        }
        return result;
    }

    std::vector<char> clean_text_data(const std::vector<char>& data) {
        std::vector<char> result;
        result.reserve(data.size());
        
        size_t start = 0;
        while (start < data.size() && (data[start] == '\r' || data[start] == '\n')) {
            start++;
        }
        
        for (size_t i = start; i < data.size(); i++) {
            if (data[i] == '\r' && i + 1 < data.size() && data[i + 1] == '\n') {
                result.push_back('\n');
                i++;
            }
            else if (data[i] == '\r' || data[i] == '\n') {
                result.push_back('\n');
            }
            else {
                result.push_back(data[i]);
            }
        }
        
        while (!result.empty() && (result.back() == '\r' || result.back() == '\n')) {
            result.pop_back();
        }
        
        return result;
    }

    bool process_text_file_upload(const std::vector<char>& file_data, 
                                const std::string& filename,
                                const std::string& current_path,
                                const std::string& username,
                                const std::string& tablet_address,
                                int part,
                                int total_parts) {
        std::vector<char> cleaned_data = clean_text_data(file_data);
        std::string file_key = File::get_file_key(current_path, filename);
        auto result = WebStorage::upload_file_chunk(username, file_key, cleaned_data, part, total_parts, tablet_address, "text/plain");
        return result.success;
    }

    bool process_binary_file_upload(const std::vector<std::byte>& file_data,
                                  const std::string& filename,
                                  const std::string& current_path,
                                  const std::string& username,
                                  const std::string& tablet_address,
                                  int part,
                                  int total_parts) {
        std::vector<char> char_data = convert_to_char_vector(file_data);
        std::string content_type = extract_header_value(file_data, "Content-Type");
        if (content_type.empty()) {
            content_type = "application/octet-stream";
        }
        std::string file_key = File::get_file_key(current_path, filename);
        auto result = WebStorage::upload_file_chunk(username, file_key, char_data, part, total_parts, tablet_address, content_type);
        return result.success;
    }
}

std::string get_username_from_session(const char* buffer) {
    const char* cookie_start = strstr(buffer, "Cookie: ");
    if (!cookie_start) return "";
    
    cookie_start += strlen("Cookie: ");
    const char* cookie_end = strstr(cookie_start, "\r\n");
    if (!cookie_end) return "";
    
    std::string cookie_str(cookie_start, cookie_end - cookie_start);
    size_t session_pos = cookie_str.find("auth_user=");
    if (session_pos == std::string::npos) return "";
    
    session_pos += strlen("auth_user=");
    std::string username = cookie_str.substr(session_pos);
    
    size_t end_pos = username.find(';');
    if (end_pos != std::string::npos) {
        username = username.substr(0, end_pos);
    }
    
    return username;
}

std::string get_current_path(const std::string& url_path) {
    std::string path = url_path.substr(8);
    
    if (path.find("/create_folder") == 0) {
        path = path.substr(strlen("/create_folder"));
    } else if (path.find("/list") == 0) {
        path = path.substr(strlen("/list"));
    } else if (path.find("/download") == 0) {
        path = path.substr(strlen("/download"));
    } else if (path.find("/delete_folder") == 0) {
        path = path.substr(strlen("/delete_folder"));
    } else if (path.find("/move") == 0) {
        path = path.substr(strlen("/move"));
    } else if (path.find("/rename") == 0) {
        path = path.substr(strlen("/rename"));
    } else if (path.find("/delete") == 0) {
        path = path.substr(strlen("/delete"));
    } else if (path.find("/upload") == 0) {
        path = path.substr(strlen("/upload"));
    }
    
    return File::normalize_path(path);
}

void handle_storage_request(int client_fd, const std::string& path, const char* buffer, bool keep_alive) {
    fprintf(stderr, "[WebStorageHandler] Received storage request: %s\n", path.c_str());
    
    if (path == "/storage" || path == "/storage/") {
        send_file_response(client_fd, "../../frontend/public/storage.html", keep_alive);
        return;
    }

    std::string username = get_username_from_session(buffer);
    if (username.empty()) {
        fprintf(stderr, "[WebStorageHandler] No valid session found\n");
        send_json_response(client_fd, false, "Not authenticated", keep_alive);
        return;
    }

    std::string tablet_address = Utils::get_tablet_for_username(username);
    if (tablet_address.empty()) {
        fprintf(stderr, "[WebStorageHandler] Failed to get tablet address for user: %s\n", username.c_str());
        send_json_response(client_fd, false, "Internal server error", keep_alive);
        return;
    }

    std::string current_path = get_current_path(path);
    fprintf(stderr, "[WebStorageHandler] Current path: %s\n", current_path.c_str());

    if (path.find("/storage/list") == 0 && buffer && strstr(buffer, "GET") != nullptr) {
        std::vector<WebStorage::FileEntry> files = WebStorage::list_files(username, tablet_address);
        std::stringstream json_response;
        json_response << "{\"files\":[";
        bool first = true;
        std::set<std::string> seen_names;
        
        bool isMoveDialog = path == "/storage/list";
        
        for (const auto& file : files) {
            if (isMoveDialog && file.type != "folder") {
                continue;
            }
            
            if (current_path == "/") {
                if (file.path.find('/') == 0 && file.path.find('/', 1) == std::string::npos) {
                    if (!file.name.empty() && seen_names.insert(file.name).second) {
                        if (!first) json_response << ",";
                        json_response << "{\"name\":\"" << file.name << "\",\"type\":\""
                                    << file.type << "\",\"path\":\"" << file.path << "\"}";
                        first = false;
                    }
                }
            } else {
                if (file.path == current_path) {
                    continue;
                }
                
                if (file.path.find(current_path + "/") == 0) {
                    std::string relative_path = file.path.substr(current_path.length() + 1);
                    if (relative_path.find('/') == std::string::npos && 
                        !file.name.empty() && seen_names.insert(file.name).second) {
                        if (!first) json_response << ",";
                        json_response << "{\"name\":\"" << file.name << "\",\"type\":\"" 
                                    << file.type << "\",\"path\":\"" << file.path << "\"}";
                        first = false;
                    }
                }
            }
        }
        
        json_response << "]}";
        send_response(client_fd, "200 OK", "application/json", json_response.str(), "", keep_alive);
        return;
    }

    if (path.find("/storage/upload") == 0 && buffer && strstr(buffer, "POST") != nullptr) {
        fprintf(stderr, "[WebStorageHandler] Processing upload request\n");
        
        std::string request(buffer);
        
        size_t content_type_pos = request.find("Content-Type: multipart/form-data");
        if (content_type_pos == std::string::npos) {
            fprintf(stderr, "[WebStorageHandler] Error: Content-Type is not multipart/form-data\n");
            send_json_response(client_fd, false, "Invalid upload request", keep_alive);
            return;
        }

        size_t boundary_pos = request.find("boundary=", content_type_pos);
        if (boundary_pos == std::string::npos) {
            fprintf(stderr, "[WebStorageHandler] Error: No boundary found\n");
            send_json_response(client_fd, false, "Invalid upload request", keep_alive);
            return;
        }

        boundary_pos += 9;
        size_t boundary_end = request.find("\r\n", boundary_pos);
        std::string boundary = request.substr(boundary_pos, boundary_end - boundary_pos);
        fprintf(stderr, "[WebStorageHandler] Boundary: %s\n", boundary.c_str());

        size_t content_length_pos = request.find("Content-Length: ");
        if (content_length_pos == std::string::npos) {
            fprintf(stderr, "[WebStorageHandler] Error: No Content-Length header\n");
            send_json_response(client_fd, false, "Invalid upload request", keep_alive);
            return;
        }

        content_length_pos += 16;
        size_t content_length_end = request.find("\r\n", content_length_pos);
        size_t content_length = std::stoul(request.substr(content_length_pos, content_length_end - content_length_pos));
        fprintf(stderr, "[WebStorageHandler] Content-Length: %zu\n", content_length);

        size_t body_start = request.find("\r\n\r\n");
        if (body_start == std::string::npos) {
            fprintf(stderr, "[WebStorageHandler] Error: No request body found\n");
            send_json_response(client_fd, false, "Invalid upload request", keep_alive);
            return;
        }
        body_start += 4;

        std::vector<std::byte> complete_data;
        complete_data.reserve(content_length);

        size_t initial_data_size = request.size() - body_start;
        for (size_t i = body_start; i < request.size(); ++i) {
            complete_data.push_back(static_cast<std::byte>(request[i]));
        }
        
        size_t total_bytes_received = initial_data_size;
        fprintf(stderr, "[WebStorageHandler] Initial data size: %zu bytes\n", initial_data_size);
        fprintf(stderr, "[WebStorageHandler] Expected content length: %zu bytes\n", content_length);

        while (total_bytes_received < content_length) {
            std::vector<std::byte> recv_buffer(8192);
            ssize_t recv_size = recv(client_fd, reinterpret_cast<char*>(recv_buffer.data()), recv_buffer.size(), 0);
            if (recv_size <= 0) {
                fprintf(stderr, "[WebStorageHandler] Error: Failed to receive complete data. Received %zu/%zu bytes\n", 
                        total_bytes_received, content_length);
                send_json_response(client_fd, false, "Failed to receive complete data", keep_alive);
                return;
            }
            complete_data.insert(complete_data.end(), recv_buffer.begin(), recv_buffer.begin() + recv_size);
            total_bytes_received += recv_size;
            // fprintf(stderr, "[WebStorageHandler] Received %zu/%zu bytes (total: %zu)\n", 
            //         recv_size, content_length, total_bytes_received);
        }

        fprintf(stderr, "[WebStorageHandler] Complete data received: %zu bytes\n", total_bytes_received);

        size_t part_start;
        bool is_final = false;
        if (!find_boundary(complete_data, boundary, 0, part_start, is_final)) {
            fprintf(stderr, "[WebStorageHandler] Error: No part boundary found in %zu bytes of data\n", total_bytes_received);
            send_json_response(client_fd, false, "Invalid upload request", keep_alive);
            return;
        }

        while (!is_final) {
            size_t headers_end;
            if (!find_headers_end(complete_data, part_start, headers_end)) {
                fprintf(stderr, "[WebStorageHandler] Error: Could not find headers end at position %zu\n", part_start);
                break;
            }

            std::string filename = extract_header_value(complete_data, "filename");
            if (!filename.empty()) {
                std::string part_str = extract_header_value(complete_data, "part");
                std::string total_str = extract_header_value(complete_data, "total");
                
                int part = 1;
                int total_parts = 1;
                
                if (!part_str.empty()) {
                    part = std::stoi(part_str);
                }
                if (!total_str.empty()) {
                    total_parts = std::stoi(total_str);
                }

                fprintf(stderr, "[WebStorageHandler] Processing file: %s (part %d/%d)\n", 
                        filename.c_str(), part, total_parts);

                size_t data_end;
                if (!find_boundary(complete_data, boundary, headers_end, data_end, is_final)) {
                    fprintf(stderr, "[WebStorageHandler] Error: Could not find data end boundary\n");
                    send_json_response(client_fd, false, "Invalid upload request", keep_alive);
                    return;
                }

                std::vector<std::byte> file_data;
                file_data.reserve(data_end - headers_end);
                file_data.assign(complete_data.begin() + headers_end, complete_data.begin() + data_end);

                fprintf(stderr, "[WebStorageHandler] File data size: %zu bytes\n", file_data.size());
                fprintf(stderr, "[WebStorageHandler] Total bytes received: %zu bytes\n", total_bytes_received);

                bool success;
                if (is_text_file(filename)) {
                    std::vector<char> text_data = convert_to_char_vector(file_data);
                    success = process_text_file_upload(text_data, filename, current_path, username, tablet_address, part, total_parts);
                } else {
                    success = process_binary_file_upload(file_data, filename, current_path, username, tablet_address, part, total_parts);
                }

                if (success) {
                    send_json_response(client_fd, true, "File uploaded successfully", keep_alive);
                } else {
                    send_json_response(client_fd, false, "Failed to upload file", keep_alive);
                }
                return;
            }

            if (!find_boundary(complete_data, boundary, part_start + 2, part_start, is_final)) {
                fprintf(stderr, "[WebStorageHandler] Error: Could not find next part boundary\n");
                break;
            }
        }

        fprintf(stderr, "[WebStorageHandler] Error: No valid file part found in %zu bytes of data\n", total_bytes_received);
        send_json_response(client_fd, false, "Invalid upload request", keep_alive);
        return;
    }

    if (path.find("/storage/delete_folder") == 0 && buffer && strstr(buffer, "POST") != nullptr) {
        std::string folder_path = path.substr(22);
        if (folder_path.empty() || folder_path == "/") {
            send_json_response(client_fd, false, "Cannot delete root folder", keep_alive);
            return;
        }
        auto result = WebStorage::delete_folder(username, folder_path, tablet_address);
        send_json_response(client_fd, result.success, result.message, keep_alive);
        return;
    } else if (path.find("/storage/delete") == 0 && buffer && strstr(buffer, "POST") != nullptr) {
        const char* content_start = strstr(buffer, "\r\n\r\n");
        if (content_start) {
            content_start += 4;
            std::string form_data(content_start);
            
            size_t filename_start = form_data.find("filename=");
            if (filename_start != std::string::npos) {
                filename_start += 9;
                std::string filename = form_data.substr(filename_start);
                while (!filename.empty() && (filename.back() == '\r' || filename.back() == '\n')) {
                    filename.pop_back();
                }
                
                std::string decoded_filename = decode_url(filename);
                std::string file_key = File::get_file_key(current_path, decoded_filename);
                auto result = WebStorage::delete_file(username, file_key, tablet_address);
                send_json_response(client_fd, result.success, result.message, keep_alive);
                return;
            }
        }
        send_json_response(client_fd, false, "Invalid delete request", keep_alive);
        return;
    }

    if (path.find("/storage/create_folder") == 0 && buffer && strstr(buffer, "POST") != nullptr) {
        const char* content_start = strstr(buffer, "\r\n\r\n");
        if (content_start) {
            content_start += 4;
            std::string form_data(content_start);
            
            size_t folder_start = form_data.find("folder=");
            if (folder_start != std::string::npos) {
                folder_start += 7;
                std::string folder_name = form_data.substr(folder_start);
                while (!folder_name.empty() && (folder_name.back() == '\r' || folder_name.back() == '\n')) {
                    folder_name.pop_back();
                }
                
                std::string decoded_folder_name = decode_url(folder_name);
                std::string folder_key = File::get_folder_key(current_path, decoded_folder_name);
                auto result = WebStorage::create_folder(username, folder_key, tablet_address);
                send_json_response(client_fd, result.success, result.message, keep_alive);
                return;
            }
        }
        send_json_response(client_fd, false, "Invalid create folder request", keep_alive);
        return;
    }

    if (path.find("/storage/download") == 0 && buffer && strstr(buffer, "GET") != nullptr) {
        std::string filename = path.substr(17);
        size_t part_pos = filename.find("?part=");
        int part = 1;
        if (part_pos != std::string::npos) {
            std::string part_str = filename.substr(part_pos + 6);
            filename = filename.substr(0, part_pos);
            part = std::stoi(part_str);
        }
        
        auto result = WebStorage::download_file_chunk(username, filename, part, tablet_address);
        
        if (result.success) {
            std::vector<WebStorage::FileEntry> files = WebStorage::list_files(username, tablet_address);
            std::string display_filename = filename;
            
            for (const auto& file : files) {
                if (file.path == filename) {
                    display_filename = file.name;
                    break;
                }
            }
            
            std::vector<char> clean_data;
            const char* data_start = nullptr;
            const char* data_end = nullptr;
            
            data_start = strstr(result.data.data(), "\r\n\r\n");
            if (data_start) {
                data_start += 4;
                data_end = strstr(data_start, "\r\n--");
                if (!data_end) {
                    data_end = result.data.data() + result.data.size();
                }
                clean_data.assign(data_start, data_end);
            } else {
                clean_data = result.data;
            }
            
            std::string content_disposition = "Content-Disposition: attachment; filename=\"" + 
                                            display_filename + "\"\r\n";
            std::string content_length = "Content-Length: " + 
                                       std::to_string(clean_data.size()) + "\r\n";
            std::string transfer_encoding = "Transfer-Encoding: chunked\r\n";
            
            std::string headers = std::string("HTTP/1.1 200 OK\r\n") +
                                std::string("Content-Type: application/octet-stream\r\n") +
                                content_disposition +
                                transfer_encoding +
                                std::string("\r\n");
            
            send(client_fd, headers.c_str(), headers.length(), 0);
            
            size_t total_sent = 0;
            const size_t CHUNK_SIZE = 4096;
            
            while (total_sent < clean_data.size()) {
                size_t remaining = clean_data.size() - total_sent;
                size_t current_chunk = std::min(CHUNK_SIZE, remaining);
                
                std::stringstream chunk_header;
                chunk_header << std::hex << current_chunk << "\r\n";
                send(client_fd, chunk_header.str().c_str(), chunk_header.str().length(), 0);
                
                ssize_t sent = send(client_fd, clean_data.data() + total_sent, current_chunk, 0);
                if (sent <= 0) {
                    fprintf(stderr, "[WebStorageHandler] [Download File] Error sending file data\n");
                    break;
                }
                send(client_fd, "\r\n", 2, 0);
                total_sent += static_cast<size_t>(sent);
            }
            send(client_fd, "0\r\n\r\n", 5, 0);
        } else {
            send_json_response(client_fd, false, "File not found", keep_alive);
        }
        return;
    }

    if (path.find("/storage/move") == 0 && buffer && strstr(buffer, "POST") != nullptr) {
        const char* content_start = strstr(buffer, "\r\n\r\n");
        if (content_start) {
            content_start += 4;
            std::string form_data(content_start);
            
            size_t item_name_start = form_data.find("item_name=");
            size_t destination_start = form_data.find("&destination=");
            size_t type_start = form_data.find("&type=");
            
            if (item_name_start != std::string::npos && destination_start != std::string::npos && type_start != std::string::npos) {
                item_name_start += 10;
                std::string item_name = form_data.substr(item_name_start, destination_start - item_name_start);
                destination_start += 13;
                std::string destination = form_data.substr(destination_start, type_start - destination_start);
                type_start += 6;
                std::string type = form_data.substr(type_start);
                
                std::string decoded_item_name = decode_url(item_name);
                std::string decoded_destination = decode_url(destination);
                
                std::string source_path = File::get_file_key(current_path, decoded_item_name);
                std::string target_path;
                
                if (decoded_destination == "/") {
                    target_path = "/" + decoded_item_name;
                } else {
                    target_path = decoded_destination + (decoded_destination.back() == '/' ? "" : "/") + decoded_item_name;
                }
                
                fprintf(stderr, "[WebStorageHandler] [Move] [%s] to [%s]\n", source_path.c_str(), target_path.c_str());

                auto result = WebStorage::move_item(username, source_path, target_path, type, tablet_address);
                send_json_response(client_fd, result.success, result.message, keep_alive);
                return;
            }
        }
        send_json_response(client_fd, false, "Invalid move request", keep_alive);
        return;
    }

    if (path.find("/storage/rename") == 0 && buffer && strstr(buffer, "POST") != nullptr) {
        const char* content_start = strstr(buffer, "\r\n\r\n");
        if (content_start) {
            content_start += 4;
            std::string form_data(content_start);
            
            size_t old_name_start = form_data.find("old_name=");
            size_t new_name_start = form_data.find("&new_name=");
            size_t type_start = form_data.find("&type=");
            
            if (old_name_start != std::string::npos && new_name_start != std::string::npos && type_start != std::string::npos) {
                old_name_start += 9;
                std::string old_name = form_data.substr(old_name_start, new_name_start - old_name_start);
                new_name_start += 10;
                std::string new_name = form_data.substr(new_name_start, type_start - new_name_start);
                type_start += 6;
                std::string type = form_data.substr(type_start);
                
                std::string decoded_old_name = decode_url(old_name);
                std::string decoded_new_name = decode_url(new_name);
                
                std::string old_path = File::get_file_key(current_path, decoded_old_name);
                std::string new_path = File::get_file_key(current_path, decoded_new_name);

                fprintf(stderr, "[WebStorageHandler] [Rename] [%s] to [%s]\n", old_name.c_str(), new_name.c_str());
                
                auto result = WebStorage::rename_item(username, old_path, new_path, type, tablet_address);
                send_json_response(client_fd, result.success, result.message, keep_alive);
                return;
            }
        }
        send_json_response(client_fd, false, "Invalid rename request", keep_alive);
        return;
    }

    send_file_response(client_fd, "../../frontend/public/storage.html", keep_alive);
}

} // namespace WebStorageHandler

