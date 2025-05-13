#include "include/utils.h"
#include <string>
#include <cstring>
#include <iostream>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sstream>

namespace Utils {

bool send_all(int socket, const char* data, size_t size) {
    size_t total_sent = 0;
    while (total_sent < size) {
        int sent = send(socket, data + total_sent, size - total_sent, 0);
        if (sent <= 0) return false;
        total_sent += sent;
    }
    return true;
}

bool recv_all(int socket, char* buffer, size_t size) {
    size_t total_received = 0;
    while (total_received < size) {
        int received = recv(socket, buffer + total_received, size - total_received, 0);
        if (received <= 0) return false;
        total_received += received;
    }
    return total_received == size;
}

std::string parse_command(const std::string& command) {
    std::string cmd, rowkey, colkey, data;
    std::istringstream iss(command);
    iss >> cmd >> rowkey >> colkey;
    
    if (cmd == "PUT") {
        std::getline(iss, data);
        data = data.substr(1);
        while (!data.empty() && (data.back() == '\r' || data.back() == '\n')) {
            data.pop_back();
        }
        return "PUT " + rowkey + " " + colkey + " " + std::to_string(data.length()) + "\r\n";
    }
    return command;
}

std::pair<std::string, bool> tablet_command(const std::string& tablet_address, const std::string& command) {
    size_t colon_pos = tablet_address.find(':');
    if (colon_pos == std::string::npos) {
        fprintf(stderr, "[tablet_command] Invalid tablet address format: %s\n", tablet_address.c_str());
        return {"", false};
    }
    
    std::string tablet_ip = tablet_address.substr(0, colon_pos);
    int tablet_port = std::stoi(tablet_address.substr(colon_pos + 1));
    
    int tablet_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (tablet_socket < 0) {
        fprintf(stderr, "[tablet_command] Failed to create tablet socket\n");
        return {"", false};
    }
    
    struct sockaddr_in tablet_addr;
    memset(&tablet_addr, 0, sizeof(tablet_addr));
    tablet_addr.sin_family = AF_INET;
    tablet_addr.sin_port = htons(tablet_port);
    
    if (inet_pton(AF_INET, tablet_ip.c_str(), &tablet_addr.sin_addr) <= 0) {
        fprintf(stderr, "[tablet_command] Invalid tablet IP address: %s\n", tablet_ip.c_str());
        close(tablet_socket);
        return {"", false};
    }
    
    if (connect(tablet_socket, (struct sockaddr*)&tablet_addr, sizeof(tablet_addr)) < 0) {
        fprintf(stderr, "[tablet_command] Failed to connect to tablet server at %s\n", tablet_address.c_str());
        close(tablet_socket);
        return {"", false};
    }
    
    char ack_buffer[1024];
    memset(ack_buffer, 0, sizeof(ack_buffer));
    if (recv(tablet_socket, ack_buffer, sizeof(ack_buffer) - 1, 0) <= 0) {
        fprintf(stderr, "[tablet_command] Failed to receive connection acknowledgment\n");
        close(tablet_socket);
        return {"", false};
    }
    
    std::string ack_response = std::string(ack_buffer);
    if (ack_response != "+OK Connected\r\n") {
        fprintf(stderr, "[tablet_command] Invalid connection acknowledgment: %s\n", ack_response.c_str());
        close(tablet_socket);
        return {"", false};
    } else {
        fprintf(stderr, "[tablet_command] Received connection acknowledgment: %s", ack_response.c_str());
    }

    
    std::string new_command = parse_command(command);
    fprintf(stderr, "[tablet_command] Sending Tablet Command: %s\n", new_command.c_str());
    
    if (!send_all(tablet_socket, new_command.c_str(), new_command.length())) {
        fprintf(stderr, "[tablet_command] Failed to send command to tablet\n");
        close(tablet_socket);
        return {"", false};
    }
    
    std::string response;
    char buffer[1024];
    memset(buffer, 0, sizeof(buffer));
    
    if (recv(tablet_socket, buffer, sizeof(buffer) - 1, 0) <= 0) {
        fprintf(stderr, "[tablet_command] Failed to receive response from tablet\n");
        close(tablet_socket);
        return {"", false};
    }
    
    response = std::string(buffer);
    
    if (command.substr(0, 4) == "GET ") {
        if (response.substr(0, 4) == "+OK ") {
            size_t bytes_pos = response.find("\r\n");
            if (bytes_pos == std::string::npos) {
                fprintf(stderr, "[tablet_command] Invalid response format: missing CRLF\n");
                close(tablet_socket);
                return {"", false};
            }
            
            try {
                size_t data_size = std::stoull(response.substr(4, bytes_pos - 4));
                
                std::string ready_msg = "READY\r\n";
                if (!send_all(tablet_socket, ready_msg.c_str(), ready_msg.length())) {
                    fprintf(stderr, "[tablet_command] Failed to send READY message\n");
                    close(tablet_socket);
                    return {"", false};
                }
                fprintf(stderr, "[tablet_command] Ready to receive data of size: %zu\n", data_size);
                
                char* data_buffer = new char[data_size + 2];
                memset(data_buffer, 0, data_size + 2);
                
                if (!recv_all(tablet_socket, data_buffer, data_size)) {
                    fprintf(stderr, "[tablet_command] Failed to receive data from tablet\n");
                    delete[] data_buffer;
                    close(tablet_socket);
                    return {"", false};
                }
                // fprintf(stderr, "[tablet_command] Received data: %s\n", data_buffer);
                response = "+OK " + std::string(data_buffer, data_size);
                delete[] data_buffer;
            } catch (const std::invalid_argument& e) {
                fprintf(stderr, "[tablet_command] Invalid data size in response: %s\n", response.c_str());
                close(tablet_socket);
                return {"", false};
            } catch (const std::out_of_range& e) {
                fprintf(stderr, "[tablet_command] Data size out of range in response: %s\n", response.c_str());
                close(tablet_socket);
                return {"", false};
            }
        }
    } else if (command.substr(0, 4) == "PUT ") {
        if (response != "+OK\r\n") {
            fprintf(stderr, "[tablet_command] Tablet did not return +OK after PUT\n");
            close(tablet_socket);
            return {"", false};
        }
        
        std::istringstream iss(command);
        std::string cmd, rowkey, colkey, data;
        iss >> cmd >> rowkey >> colkey;
        std::getline(iss, data);
        data = data.substr(1);
        
        if (!send_all(tablet_socket, data.c_str(), data.length())) {
            fprintf(stderr, "[tablet_command] Failed to send data to tablet\n");
            close(tablet_socket);
            return {"", false};
        }
        
        memset(buffer, 0, sizeof(buffer));
        if (recv(tablet_socket, buffer, sizeof(buffer) - 1, 0) <= 0) {
            fprintf(stderr, "[tablet_command] Failed to receive response after sending data\n");
            close(tablet_socket);
            return {"", false};
        }
        response = std::string(buffer);
        if (response.substr(0, 5) != "-ERR " && response.substr(0, 4) != "+OK ") {
            response = "+OK " + response;
        }
    }
    
    // fprintf(stderr, "[tablet_command] Tablet Response: %s", response.c_str());
    close(tablet_socket);
    return {response, true};
}

} // namespace Utils
