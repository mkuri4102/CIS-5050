#ifndef CLIENT_LIB_H
#define CLIENT_LIB_H

#include <string>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace ClientLib {

struct ServerInfo {
    std::string ip;
    int port;
};

// Simple function to extract a string value from a JSON-like string
std::string extract_json_string(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) {
        throw std::runtime_error("Key not found: " + key);
    }
    
    // Find the opening quote after the colon
    pos = json.find(":", pos) + 1;
    // Skip whitespace
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) {
        pos++;
    }
    
    if (pos >= json.length() || json[pos] != '"') {
        throw std::runtime_error("Invalid JSON format for string value");
    }
    
    // Find the closing quote that isn't escaped
    size_t end_pos = pos + 1;
    while (end_pos < json.length()) {
        if (json[end_pos] == '"' && json[end_pos-1] != '\\') {
            break;
        }
        end_pos++;
    }
    
    if (end_pos >= json.length()) {
        throw std::runtime_error("Unterminated string in JSON");
    }
    
    return json.substr(pos + 1, end_pos - pos - 1);
}

// Simple function to extract an integer value from a JSON-like string
int extract_json_int(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) {
        throw std::runtime_error("Key not found: " + key);
    }
    
    // Find the colon after the key
    pos = json.find(":", pos) + 1;
    // Skip whitespace
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) {
        pos++;
    }
    
    // Find the end of the number (space, comma, or closing brace)
    size_t end_pos = pos;
    while (end_pos < json.length() && json[end_pos] != ' ' && json[end_pos] != ',' && json[end_pos] != '}' && json[end_pos] != '\n' && json[end_pos] != '\r') {
        end_pos++;
    }
    
    std::string number_str = json.substr(pos, end_pos - pos);
    try {
        return std::stoi(number_str);
    } catch (const std::exception& e) {
        throw std::runtime_error("Invalid number format in JSON");
    }
}

/**
 * Connect to the load balancer to get a server assignment
 * @param lb_ip The IP address of the load balancer
 * @param lb_port The port of the load balancer
 * @return ServerInfo struct containing the assigned server's IP and port
 * @throws std::runtime_error if connection fails or response is invalid
 */
ServerInfo get_server_assignment(const std::string& lb_ip = "127.0.0.1", int lb_port = 8080) {
    // Create socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        throw std::runtime_error("Failed to create socket");
    }
    
    // Set up server address
    struct sockaddr_in lb_addr;
    memset(&lb_addr, 0, sizeof(lb_addr));
    lb_addr.sin_family = AF_INET;
    lb_addr.sin_port = htons(lb_port);
    
    if (inet_pton(AF_INET, lb_ip.c_str(), &lb_addr.sin_addr) <= 0) {
        close(sock);
        throw std::runtime_error("Invalid load balancer address");
    }
    
    // Connect to load balancer
    if (connect(sock, (struct sockaddr*)&lb_addr, sizeof(lb_addr)) < 0) {
        close(sock);
        throw std::runtime_error("Connection to load balancer failed");
    }
    
    // Send a simple HTTP request to the load balancer
    std::string request = "GET / HTTP/1.1\r\nHost: " + lb_ip + "\r\nConnection: close\r\n\r\n";
    if (send(sock, request.c_str(), request.length(), 0) < 0) {
        close(sock);
        throw std::runtime_error("Failed to send request to load balancer");
    }
    
    // Receive response
    const int BUFFER_SIZE = 4096;
    char buffer[BUFFER_SIZE];
    std::string response;
    
    while (true) {
        int bytes = recv(sock, buffer, BUFFER_SIZE - 1, 0);
        if (bytes <= 0) break;
        buffer[bytes] = '\0';
        response += buffer;
    }
    
    close(sock);
    
    // Parse response to extract the body (JSON part)
    size_t body_pos = response.find("\r\n\r\n");
    if (body_pos == std::string::npos) {
        throw std::runtime_error("Invalid response from load balancer");
    }
    
    std::string body = response.substr(body_pos + 4);
    
    try {
        // Extract server info from the JSON manually
        ServerInfo server;
        // First find the server object, then extract from within it
        size_t server_pos = body.find("\"server\"");
        if (server_pos == std::string::npos) {
            throw std::runtime_error("Server information not found in response");
        }
        // Find the opening brace of the server object
        server_pos = body.find("{", server_pos);
        if (server_pos == std::string::npos) {
            throw std::runtime_error("Invalid server object format");
        }
        // Find the closing brace of the server object
        size_t server_end_pos = server_pos + 1;
        int brace_count = 1;
        while (server_end_pos < body.length() && brace_count > 0) {
            if (body[server_end_pos] == '{') brace_count++;
            else if (body[server_end_pos] == '}') brace_count--;
            server_end_pos++;
        }
        if (brace_count != 0) {
            throw std::runtime_error("Unterminated server object");
        }
        
        // Extract the server object as a substring
        std::string server_obj = body.substr(server_pos, server_end_pos - server_pos);
        
        server.ip = extract_json_string(server_obj, "ip");
        server.port = extract_json_int(server_obj, "port");
        
        return server;
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to parse server info: " + std::string(e.what()));
    }
}

/**
 * Connect to a server and send an HTTP request
 * @param server The ServerInfo struct containing the server's IP and port
 * @param path The HTTP path to request
 * @param method The HTTP method (GET, POST, etc.)
 * @param body The request body (for POST requests)
 * @return The HTTP response as a string
 * @throws std::runtime_error if connection fails
 */
std::string send_request(const ServerInfo& server, const std::string& path, 
                        const std::string& method = "GET", const std::string& body = "") {
    // Create socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        throw std::runtime_error("Failed to create socket");
    }
    
    // Set up server address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server.port);
    
    if (inet_pton(AF_INET, server.ip.c_str(), &server_addr.sin_addr) <= 0) {
        close(sock);
        throw std::runtime_error("Invalid server address: " + server.ip);
    }
    
    // Connect to server
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        close(sock);
        throw std::runtime_error("Connection to server failed");
    }
    
    // Prepare HTTP request
    std::ostringstream request_stream;
    request_stream << method << " " << path << " HTTP/1.1\r\n";
    request_stream << "Host: " << server.ip << ":" << server.port << "\r\n";
    request_stream << "Connection: close\r\n";
    
    if (!body.empty() && method == "POST") {
        request_stream << "Content-Type: application/json\r\n";
        request_stream << "Content-Length: " << body.length() << "\r\n";
        request_stream << "\r\n" << body;
    } else {
        request_stream << "\r\n";
    }
    
    std::string request = request_stream.str();
    
    // Send request
    if (send(sock, request.c_str(), request.length(), 0) < 0) {
        close(sock);
        throw std::runtime_error("Failed to send request to server");
    }
    
    // Receive response
    const int BUFFER_SIZE = 4096;
    char buffer[BUFFER_SIZE];
    std::string response;
    
    while (true) {
        int bytes = recv(sock, buffer, BUFFER_SIZE - 1, 0);
        if (bytes <= 0) break;
        buffer[bytes] = '\0';
        response += buffer;
    }
    
    close(sock);
    return response;
}

/**
 * Helper function to make an HTTP request via the load balancer
 * This combines get_server_assignment and send_request
 */
std::string make_request(const std::string& path, const std::string& method = "GET", 
                        const std::string& body = "", const std::string& lb_ip = "127.0.0.1", 
                        int lb_port = 8080) {
    // Get server assignment from load balancer
    ServerInfo server = get_server_assignment(lb_ip, lb_port);
    
    // Send request to assigned server
    return send_request(server, path, method, body);
}

} // namespace ClientLib

#endif // CLIENT_LIB_H 