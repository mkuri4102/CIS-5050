#include "include/routes/routes.h"
#include "include/utils.h"

#include <string>
#include <sstream>
#include <vector>
#include <sys/socket.h>
#include <iostream>
#include <algorithm>

/*
 * Admin Database Handler
 * 
 * Provides functionality for the admin dashboard to interact with the database.
 * Implements:
 * - Row retrieval across multiple tablet servers
 * - Column retrieval for specific rows
 * - Value retrieval for specific row/column pairs
 * - Error handling for missing tablets and data
 * - JSON response formatting
 */

// Keep alive timeout in seconds
#define KEEP_ALIVE_TIMEOUT 15

namespace Routes {


std::vector<std::string> parse_space_separated_values(const std::string& data) {
    std::vector<std::string> values;
    std::istringstream iss(data);
    std::string value;
    
    while (iss >> value) {
        if (!value.empty()) {
            values.push_back(value);
        }
    }
    
    return values;
}

void handle_get_db_rows(int client_fd, bool keep_alive) {
    std::cout << "Admin request: Getting all database rows" << std::endl;
    
    std::vector<std::string> tablet_addresses;
    
    
    for (int i = 0; i < 3; i++) {
        std::string dummy_key = "dummy_key_" + std::to_string(i);
        std::string tablet_address = Utils::get_tablet_for_username(dummy_key);
        
        if (tablet_address == "SERVICE_DOWN") {
            
            continue;
        }
        
        if (!tablet_address.empty() && 
            std::find(tablet_addresses.begin(), tablet_addresses.end(), tablet_address) == tablet_addresses.end()) {
            tablet_addresses.push_back(tablet_address);
        }
    }
    
    
    std::vector<std::string> known_users = {"admin", "user1", "test"};
    for (const auto& user : known_users) {
        std::string tablet_address = Utils::get_tablet_for_username(user);
        
        if (tablet_address == "SERVICE_DOWN") {
            
            continue;
        }
        
        if (!tablet_address.empty() && 
            std::find(tablet_addresses.begin(), tablet_addresses.end(), tablet_address) == tablet_addresses.end()) {
            tablet_addresses.push_back(tablet_address);
        }
    }
    
    
    if (tablet_addresses.empty()) {
        std::string error_text = "{\"error\": \"No active tablet servers found\"}";
        std::string response = "HTTP/1.1 500 Internal Server Error\r\n"
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
        std::cout << "HTTP Response: No active tablet servers" << std::endl;
        return;
    }
    
    
    std::vector<std::string> all_rows;
    for (const auto& tablet_address : tablet_addresses) {
        std::cout << "Querying tablet server: " << tablet_address << std::endl;
        
        std::string get_rows_cmd = "GET_ROWS\r\n";
        auto [rows_response, success] = Utils::tablet_command(tablet_address, get_rows_cmd);
        
        if (success) {
            std::cout << "Tablet response: " << rows_response << std::endl;
            
            if (rows_response.find("+OK") == 0) {
                std::string rows_data = rows_response.substr(4); 
                if (rows_data.length() >= 2 && rows_data.substr(rows_data.length() - 2) == "\r\n") {
                    rows_data = rows_data.substr(0, rows_data.length() - 2);
                }
                
                
                std::vector<std::string> tablet_rows = parse_space_separated_values(rows_data);
                all_rows.insert(all_rows.end(), tablet_rows.begin(), tablet_rows.end());
            }
        } else {
            std::cout << "Failed to get rows from tablet: " << tablet_address << std::endl;
        }
    }
    
    
    std::ostringstream json;
    json << "{\"data\":[";
    for (size_t i = 0; i < all_rows.size(); ++i) {
        json << "\"" << all_rows[i] << "\"";
        if (i < all_rows.size() - 1) {
            json << ",";
        }
    }
    json << "]}";
    
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
    std::cout << "HTTP Response: Sent " << all_rows.size() << " rows" << std::endl;
}

void handle_get_db_columns(int client_fd, const std::string& path, bool keep_alive) {
    
    size_t pos = path.find("row=");
    if (pos == std::string::npos) {
        std::string error_text = "{\"error\": \"Missing row parameter\"}";
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
        std::cout << "HTTP Response: Missing row parameter" << std::endl;
        return;
    }
    
    std::string row = path.substr(pos + 4);
    pos = row.find('&');
    if (pos != std::string::npos) {
        row = row.substr(0, pos);
    }
    
    std::cout << "Admin request: Getting columns for row: " << row << std::endl;
    
    
    std::string tablet_address = Utils::get_tablet_for_username(row);
    
    if (tablet_address == "SERVICE_DOWN") {
        std::string error_text = "{\"error\": \"Service unavailable for this data shard\"}";
        std::string response = "HTTP/1.1 503 Service Unavailable\r\n"
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
        std::cout << "HTTP Response: Service unavailable for this data shard" << std::endl;
        return;
    }
    
    if (tablet_address.empty()) {
        std::string error_text = "{\"error\": \"Could not determine tablet server for row\"}";
        std::string response = "HTTP/1.1 500 Internal Server Error\r\n"
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
        std::cout << "HTTP Response: Could not determine tablet server" << std::endl;
        return;
    }
    
    
    std::string get_cols_cmd = "GET_COLS " + row + "\r\n";
    auto [cols_response, success] = Utils::tablet_command(tablet_address, get_cols_cmd);
    std::cout << "Tablet response for GET_COLS: " << cols_response << std::endl;
    
    if (!success) {
        std::string error_text = "{\"error\": \"Failed to communicate with tablet server\"}";
        std::string response = "HTTP/1.1 500 Internal Server Error\r\n"
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
        std::cout << "HTTP Response: Failed to communicate with tablet" << std::endl;
        return;
    }
    
    if (cols_response.find("+OK") == 0) {
        
        std::string col_data = cols_response.substr(4); 
        if (col_data.length() >= 2 && col_data.substr(col_data.length() - 2) == "\r\n") {
            col_data = col_data.substr(0, col_data.length() - 2);
        }
        
        std::vector<std::string> columns = parse_space_separated_values(col_data);
        
        
        std::ostringstream json;
        json << "{\"data\":[";
        for (size_t i = 0; i < columns.size(); ++i) {
            json << "\"" << columns[i] << "\"";
            if (i < columns.size() - 1) {
                json << ",";
            }
        }
        json << "]}";
        
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
        std::cout << "HTTP Response: Sent " << columns.size() << " columns for row " << row << std::endl;
    } else {
        
        std::string error_text = "{\"error\": \"Row not found or other error\"}";
        std::string response = "HTTP/1.1 404 Not Found\r\n"
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
        std::cout << "HTTP Response: Row not found - " << cols_response << std::endl;
    }
}

void handle_get_db_value(int client_fd, const std::string& path, bool keep_alive) {
    
    size_t row_pos = path.find("row=");
    size_t col_pos = path.find("col=");
    
    if (row_pos == std::string::npos || col_pos == std::string::npos) {
        std::string error_text = "{\"error\": \"Missing row or column parameter\"}";
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
        std::cout << "HTTP Response: Missing row or column parameter" << std::endl;
        return;
    }
    
    std::string row = path.substr(row_pos + 4);
    size_t row_end = row.find('&');
    if (row_end != std::string::npos) {
        row = row.substr(0, row_end);
    }
    
    std::string col = path.substr(col_pos + 4);
    size_t col_end = col.find('&');
    if (col_end != std::string::npos) {
        col = col.substr(0, col_end);
    }
    
    std::cout << "Admin request: Getting value for row: " << row << ", col: " << col << std::endl;
    
    
    std::string tablet_address = Utils::get_tablet_for_username(row);
    
    if (tablet_address == "SERVICE_DOWN") {
        std::string error_text = "{\"error\": \"Service unavailable for this data shard\"}";
        std::string response = "HTTP/1.1 503 Service Unavailable\r\n"
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
        std::cout << "HTTP Response: Service unavailable for this data shard" << std::endl;
        return;
    }
    
    if (tablet_address.empty()) {
        std::string error_text = "{\"error\": \"Could not determine tablet server for row\"}";
        std::string response = "HTTP/1.1 500 Internal Server Error\r\n"
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
        std::cout << "HTTP Response: Could not determine tablet server" << std::endl;
        return;
    }
    
    
    std::string get_value_cmd = "GET " + row + " " + col + "\r\n";
    auto [value_response, success] = Utils::tablet_command(tablet_address, get_value_cmd);
    std::cout << "Tablet response for GET: " << value_response << std::endl;
    
    if (!success) {
        std::string error_text = "{\"error\": \"Failed to communicate with tablet server\"}";
        std::string response = "HTTP/1.1 500 Internal Server Error\r\n"
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
        std::cout << "HTTP Response: Failed to communicate with tablet" << std::endl;
        return;
    }
    
    if (value_response.find("+OK ") == 0) {
        
        std::string value = value_response.substr(4); 
        if (value.length() >= 2 && value.substr(value.length() - 2) == "\r\n") {
            value = value.substr(0, value.length() - 2);
        }
        
        
        std::ostringstream json;
        json << "{\"value\":\"" << value << "\"}";
        
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
        std::cout << "HTTP Response: Sent value for row " << row << ", col " << col << std::endl;
    } else {
        
        std::string error_text = "{\"error\": \"Value not found or other error\"}";
        std::string response = "HTTP/1.1 404 Not Found\r\n"
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
        std::cout << "HTTP Response: Value not found - " << value_response << std::endl;
    }
}

} 
