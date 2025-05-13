#include "include/routes/routes.h"
#include "include/utils.h"
#include <sstream>
#include <string>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <vector>
#include <map>
#include <iostream>
#include <cstring>

/*
 * Admin Nodes Handler
 * 
 * Provides functionality for the admin dashboard to monitor and manage system nodes.
 * Implements:
 * - Node status checking and reporting
 * - Data retrieval from tablet nodes
 * - Frontend server status monitoring
 * - JSON response formatting with proper escaping
 * - Port availability checking
 */

// Keep alive timeout in seconds
#define KEEP_ALIVE_TIMEOUT 15

namespace Routes {
    
    std::string escape_json_string(const std::string& input) {
        std::ostringstream escaped;
        for (char c : input) {
            switch (c) {
                case '\"': escaped << "\\\""; break;
                case '\\': escaped << "\\\\"; break;
                case '\b': escaped << "\\b"; break;
                case '\f': escaped << "\\f"; break;
                case '\n': escaped << "\\n"; break;
                case '\r': escaped << "\\r"; break;
                case '\t': escaped << "\\t"; break;
                default:
                    if (c < 32) {
                        char buffer[7];
                        snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                        escaped << buffer;
                    } else {
                        escaped << c;
                    }
            }
        }
        return escaped.str();
    }

    bool is_port_open(int port) {
        struct sockaddr_in addr;
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) return false;

        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");
        addr.sin_port = htons(port);

        int result = connect(sock, (struct sockaddr*)&addr, sizeof(addr));
        close(sock);
        
        return result == 0;
    }

    std::string get_frontend_status() {
        std::ostringstream status;
        status << "[\n";
        
        int ports[] = {8081, 8082, 8083};
        for (int i = 0; i < 3; i++) {
            int port = ports[i];
            bool is_alive = is_port_open(port);
            
            status << "  {\n"
                   << "    \"serverId\": " << (i + 1) << ",\n"
                   << "    \"port\": " << port << ",\n"
                   << "    \"status\": \"" << (is_alive ? "Alive" : "Unreachable") << "\"\n"
                   << "  }";
            
            if (i < 2) status << ",";
            status << "\n";
        }
        
        status << "]";
        return status.str();
    }

    void handle_admin_nodes(int client_fd, bool keep_alive) {
        std::string nodes_response = Utils::kvstore_command("LIST_NODES\r\n");
        
        std::string frontend_status = get_frontend_status();
        
        std::ostringstream response_stream;
        response_stream << "HTTP/1.1 200 OK\r\n"
                      << "Content-Type: application/json\r\n";
        
        if (keep_alive) {
            response_stream << "Connection: keep-alive\r\n"
                          << "Keep-Alive: timeout=" << KEEP_ALIVE_TIMEOUT << "\r\n";
        } else {
            response_stream << "Connection: close\r\n";
        }
        
        std::string json_response;
        
        if (nodes_response.find("+OK") == 0) {
            json_response = "{\n  \"success\": true,\n  \"nodes\": \"" 
                          + escape_json_string(nodes_response) 
                          + "\",\n  \"frontendServers\": " 
                          + frontend_status 
                          + "\n}";
        } else {
            json_response = "{\n  \"success\": false,\n  \"error\": \"Failed to retrieve node status\",\n"
                          "  \"frontendServers\": " + frontend_status + "\n}";
        }
        
        response_stream << "Content-Length: " << json_response.length() << "\r\n"
                      << "\r\n"
                      << json_response;
        
        std::string response = response_stream.str();
        send(client_fd, response.c_str(), response.length(), 0);
    }

    void handle_admin_node_data(int client_fd, const std::string& path, bool keep_alive) {
        size_t pos = path.find("nodeId=");
        if (pos == std::string::npos) {
            std::string error_text = "{\"success\": false, \"error\": \"Missing nodeId parameter\"}";
            std::string response = "HTTP/1.1 400 Bad Request\r\n"
                               "Content-Type: application/json\r\n"
                               "Content-Length: " + std::to_string(error_text.size()) + "\r\n";
            
            if (keep_alive) {
                response += "Connection: keep-alive\r\n"
                           "Keep-Alive: timeout=" + std::to_string(KEEP_ALIVE_TIMEOUT) + "\r\n";
            } else {
                response += "Connection: close\r\n";
            }
            
            response += "\r\n" + error_text;
            
            send(client_fd, response.c_str(), response.size(), 0);
            return;
        }
        
        std::string nodeIdStr = path.substr(pos + 7);
        pos = nodeIdStr.find('&');
        if (pos != std::string::npos) {
            nodeIdStr = nodeIdStr.substr(0, pos);
        }
        
        int nodeId;
        try {
            nodeId = std::stoi(nodeIdStr);
        } catch (const std::exception& e) {
            std::string error_text = "{\"success\": false, \"error\": \"Invalid nodeId parameter\"}";
            std::string response = "HTTP/1.1 400 Bad Request\r\n"
                               "Content-Type: application/json\r\n"
                               "Content-Length: " + std::to_string(error_text.size()) + "\r\n";
            
            if (keep_alive) {
                response += "Connection: keep-alive\r\n"
                           "Keep-Alive: timeout=" + std::to_string(KEEP_ALIVE_TIMEOUT) + "\r\n";
            } else {
                response += "Connection: close\r\n";
            }
            
            response += "\r\n" + error_text;
            
            send(client_fd, response.c_str(), response.size(), 0);
            return;
        }
        
        int port = 6000 + nodeId;
        std::string tablet_address = "127.0.0.1:" + std::to_string(port);
        
        std::cout << "Admin request: Getting data from node " << nodeId << " at " << tablet_address << std::endl;
        
        std::vector<std::string> rows;
        std::map<std::string, std::vector<std::string>> columns;
        std::map<std::string, std::map<std::string, std::string>> values;
        
        std::string get_rows_cmd = "GET_ROWS\r\n";
        auto [rows_response, rows_success] = Utils::tablet_command(tablet_address, get_rows_cmd);
        
        if (!rows_success) {
            std::string error_text = "{\"success\": false, \"error\": \"Failed to communicate with tablet node " + 
                std::to_string(nodeId) + "\"}";
            std::string response = "HTTP/1.1 502 Bad Gateway\r\n"
                               "Content-Type: application/json\r\n"
                               "Content-Length: " + std::to_string(error_text.size()) + "\r\n";
            
            if (keep_alive) {
                response += "Connection: keep-alive\r\n"
                           "Keep-Alive: timeout=" + std::to_string(KEEP_ALIVE_TIMEOUT) + "\r\n";
            } else {
                response += "Connection: close\r\n";
            }
            
            response += "\r\n" + error_text;
            
            send(client_fd, response.c_str(), response.size(), 0);
            return;
        }
        
        if (rows_response.find("+OK") == 0) {
            std::string rows_data = rows_response.substr(4);
            if (rows_data.length() >= 2 && rows_data.substr(rows_data.length() - 2) == "\r\n") {
                rows_data = rows_data.substr(0, rows_data.length() - 2);
            }
            
            std::istringstream iss(rows_data);
            std::string row;
            while (iss >> row) {
                if (!row.empty()) {
                    rows.push_back(row);
                }
            }
        }
        
        for (const auto& row : rows) {
            std::string get_cols_cmd = "GET_COLS " + row + "\r\n";
            auto [cols_response, cols_success] = Utils::tablet_command(tablet_address, get_cols_cmd);
            
            if (cols_success && cols_response.find("+OK") == 0) {
                std::string cols_data = cols_response.substr(4);
                if (cols_data.length() >= 2 && cols_data.substr(cols_data.length() - 2) == "\r\n") {
                    cols_data = cols_data.substr(0, cols_data.length() - 2);
                }
                
                std::vector<std::string> row_columns;
                std::istringstream iss(cols_data);
                std::string col;
                while (iss >> col) {
                    if (!col.empty()) {
                        row_columns.push_back(col);
                        
                        std::string get_val_cmd = "GET " + row + " " + col + "\r\n";
                        auto [val_response, val_success] = Utils::tablet_command(tablet_address, get_val_cmd);
                        
                        if (val_success && val_response.find("+OK ") == 0) {
                            std::string value = val_response.substr(4);
                            
                            if (value.length() >= 2 && value.substr(value.length() - 2) == "\r\n") {
                                value = value.substr(0, value.length() - 2);
                            }
                            
                            values[row][col] = value;
                            std::cout << "Got value for " << row << "/" << col << ": " << value << std::endl;
                        }
                    }
                }
                
                columns[row] = row_columns;
            }
        }
        
        std::ostringstream json;
        json << "{\n";
        json << "  \"success\": true,\n";
        
        json << "  \"rows\": [";
        for (size_t i = 0; i < rows.size(); ++i) {
            json << "\"" << escape_json_string(rows[i]) << "\"";
            if (i < rows.size() - 1) {
                json << ", ";
            }
        }
        json << "],\n";
        
        json << "  \"columns\": {\n";
        size_t row_count = 0;
        for (const auto& row_pair : columns) {
            json << "    \"" << escape_json_string(row_pair.first) << "\": [";
            for (size_t i = 0; i < row_pair.second.size(); ++i) {
                json << "\"" << escape_json_string(row_pair.second[i]) << "\"";
                if (i < row_pair.second.size() - 1) {
                    json << ", ";
                }
            }
            json << "]";
            if (row_count < columns.size() - 1) {
                json << ",";
            }
            json << "\n";
            row_count++;
        }
        json << "  },\n";
        
        json << "  \"values\": {\n";
        row_count = 0;
        for (const auto& row_pair : values) {
            json << "    \"" << escape_json_string(row_pair.first) << "\": {\n";
            size_t col_count = 0;
            for (const auto& col_pair : row_pair.second) {
                std::string escaped_value = escape_json_string(col_pair.second);
                
                json << "      \"" << escape_json_string(col_pair.first) << "\": \"" 
                     << escaped_value << "\"";
                if (col_count < row_pair.second.size() - 1) {
                    json << ",";
                }
                json << "\n";
                col_count++;
            }
            json << "    }";
            if (row_count < values.size() - 1) {
                json << ",";
            }
            json << "\n";
            row_count++;
        }
        json << "  }\n";
        
        json << "}";
        
        std::string json_str = json.str();
        std::string response = "HTTP/1.1 200 OK\r\n"
                             "Content-Type: application/json\r\n"
                             "Content-Length: " + std::to_string(json_str.size()) + "\r\n";
        
        if (keep_alive) {
            response += "Connection: keep-alive\r\n"
                       "Keep-Alive: timeout=" + std::to_string(KEEP_ALIVE_TIMEOUT) + "\r\n";
        } else {
            response += "Connection: close\r\n";
        }
        
        response += "\r\n" + json_str;
        
        send(client_fd, response.c_str(), response.size(), 0);
        std::cout << "HTTP Response: Sent data from node " << nodeId << " with " << rows.size() << " rows" << std::endl;
    }
} 
