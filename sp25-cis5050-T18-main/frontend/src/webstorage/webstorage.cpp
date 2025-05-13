#include "include/webstorage/webstorage.hpp"
#include "include/utils.h"
#include <sstream>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace WebStorage {

namespace {
    StorageResult process_tablet_response(const std::string& response, const std::string& success_message) {
        StorageResult result;
        if (response.substr(0, 4) == "+OK ") {
            result.success = true;
            result.message = success_message;
        } else {
            result.success = false;
            result.message = "Operation failed: " + response;
        }
        return result;
    }

    std::string hex_encode(const std::vector<char>& data) {
        std::string hex_str;
        hex_str.reserve(data.size() * 2);
        for (unsigned char c : data) {
            char hex[3];
            snprintf(hex, sizeof(hex), "%02x", c);
            hex_str += hex;
        }
        return hex_str;
    }

    std::vector<char> hex_decode(const std::string& hex_str) {
        std::vector<char> data;
        data.reserve(hex_str.length() / 2);
        for (size_t i = 0; i < hex_str.length(); i += 2) {
            if (i + 1 >= hex_str.length()) break;
            std::string byte_str = hex_str.substr(i, 2);
            char byte = (char)strtol(byte_str.c_str(), nullptr, 16);
            data.push_back(byte);
        }
        return data;
    }
}

StorageResult upload_file_chunk(const std::string& username,
                              const std::string& filename,
                              const std::vector<char>& chunk_data,
                              int part,
                              int total_parts,
                              const std::string& tablet_address,
                              const std::string& content_type) {
    std::string metadata = File::create_file_metadata(part, total_parts, content_type);
    std::stringstream cmd;
    cmd << "PUT " << username << " " << filename << " " << metadata;

    fprintf(stderr, "[WebStorage] Chunked: PUT %s %s (part %d/%d)\n",
            username.c_str(), filename.c_str(), part, total_parts);

    auto [response, success] = Utils::tablet_command(tablet_address, cmd.str());
    if (response.substr(0, 4) != "+OK ") {
        return {false, "Failed to upload chunk metadata: " + response, {}};
    }

    cmd.str("");
    cmd << "PUT " << username << " " << metadata << " " << hex_encode(chunk_data);

    fprintf(stderr, "[WebStorage] Chunked: PUT %s %s (chunk size: %zu bytes)\n",
            username.c_str(), metadata.c_str(), chunk_data.size());

    auto [data_response, data_success] = Utils::tablet_command(tablet_address, cmd.str());
    return process_tablet_response(data_response, "Chunk uploaded successfully");
}

StorageResult download_file_chunk(const std::string& username,
                                const std::string& filename,
                                int part,
                                const std::string& tablet_address) {
    std::stringstream cmd;
    cmd << "GET " << username << " " << filename;
    
    fprintf(stderr, "[WebStorage] Requesting file metadata: GET %s %s\n",
            username.c_str(), filename.c_str());

    auto [response, success] = Utils::tablet_command(tablet_address, cmd.str());
    if (response.substr(0, 4) != "+OK ") {
        return {false, "Download failed: " + response, {}};
    }

    std::string metadata_str = response.substr(4);
    File::Metadata metadata = File::Metadata::from_string(metadata_str);
    metadata.part = part;
    std::string modified_metadata = metadata.to_string();

    cmd.str("");
    cmd << "GET " << username << " " << modified_metadata;
    
    fprintf(stderr, "[WebStorage] Requesting chunk data: GET %s %s (part %d)\n",
            username.c_str(), modified_metadata.c_str(), part);

    auto [data_response, data_success] = Utils::tablet_command(tablet_address, cmd.str());
    if (data_response.substr(0, 4) != "+OK ") {
        return {false, "Download failed: " + data_response, {}};
    }

    std::string hex_data = data_response.substr(4);
    while (!hex_data.empty() && (hex_data.back() == '\r' || hex_data.back() == '\n' || hex_data.back() == ' ')) {
        hex_data.pop_back();
    }

    StorageResult result;
    result.success = true;
    result.data = hex_decode(hex_data);
    return result;
}

bool file_exists(const std::string& username, 
                const std::string& filename,
                const std::string& tablet_address) {
    std::stringstream cmd;
    cmd << "GET " << username << " " << filename;
    auto [response, success] = Utils::tablet_command(tablet_address, cmd.str());
    return response.substr(0, 4) == "+OK ";
}

std::vector<FileEntry> list_files(const std::string& username,
                                const std::string& tablet_address) {
    std::vector<FileEntry> files;
    std::stringstream cmd;
    cmd << "GET_COLS " << username;

    auto [response, success] = Utils::tablet_command(tablet_address, cmd.str());
    
    if (response.substr(0, 4) == "+OK ") {
        std::string file_list = response.substr(4);
        
        size_t pos = 0;
        while ((pos = file_list.find(' ')) != std::string::npos) {
            std::string path = file_list.substr(0, pos);
            if (path.substr(0, 1) == "/" && !path.empty()) {
                FileEntry entry;
                
                if (path.back() == '/') {
                    entry.type = "folder";
                    entry.path = path.substr(0, path.length() - 1);
                } else {
                    entry.type = "file";
                    entry.path = path;
                }

                size_t last_slash = entry.path.find_last_of('/');
                entry.name = entry.path.substr(last_slash + 1);
                
                entry.metadata = File::Metadata();
                files.push_back(entry);
            }
            file_list.erase(0, pos + 1);
        }
        
        if (!file_list.empty() && file_list.substr(0, 1) == "/") {
            FileEntry entry;
            
            if (file_list.back() == '/') {
                entry.type = "folder";
                entry.path = file_list.substr(0, file_list.length() - 1);
            } else {
                entry.type = "file";
                entry.path = file_list;
            }
            
            size_t last_slash = entry.path.find_last_of('/');
            entry.name = entry.path.substr(last_slash + 1);
            
            entry.metadata = File::Metadata();
            files.push_back(entry);
        }
    }
    return files;
}

StorageResult delete_file(const std::string& username,
                        const std::string& filename,
                        const std::string& tablet_address) {
    std::stringstream cmd;
    cmd << "GET " << username << " " << filename;
    
    auto [response, success] = Utils::tablet_command(tablet_address, cmd.str());
    if (response.substr(0, 4) != "+OK ") {
        return {false, "File not found: " + response, {}};
    }

    std::string metadata_str = response.substr(4);
    
    cmd.str("");
    cmd << "DELETE " << username << " " << metadata_str;
    auto [delete_response, delete_success] = Utils::tablet_command(tablet_address, cmd.str());
    
    cmd.str("");
    cmd << "DELETE " << username << " " << filename;
    auto [final_response, final_success] = Utils::tablet_command(tablet_address, cmd.str());
    
    return process_tablet_response(final_response, "File deleted successfully");
}

StorageResult create_folder(const std::string& username,
                          const std::string& folder_path,
                          const std::string& tablet_address) {
    std::string full_path = folder_path + "/";
    
    std::string metadata = File::create_folder_metadata();
    std::stringstream cmd;
    cmd << "PUT " << username << " " << full_path << " " << metadata;
    
    fprintf(stderr, "[WebStorage] Creating folder: %s\n", full_path.c_str());
    
    auto [response, success] = Utils::tablet_command(tablet_address, cmd.str());
    return process_tablet_response(response, "Folder created successfully");
}

StorageResult delete_folder(const std::string& username,
                          const std::string& folder_path,
                          const std::string& tablet_address) {
    std::string normalized_path = File::normalize_path(folder_path) + "/";
    
    fprintf(stderr, "[delete_folder] Starting deletion of folder: %s\n", normalized_path.c_str());
    
    std::vector<FileEntry> all_files = list_files(username, tablet_address);
    std::vector<std::string> files_to_delete, folders_to_delete;
    
    for (const auto& file : all_files) {
        if (file.path.find(normalized_path) == 0) {
            if (file.type == "folder") {
                folders_to_delete.push_back(file.path + "/");
            } else {
                files_to_delete.push_back(file.path);
            }
        }
    }
        
    for (const auto& path : files_to_delete) {
        StorageResult delete_result;
        delete_result = delete_file(username, path, tablet_address);
        if (!delete_result.success) {
            fprintf(stderr, "[delete_folder] Failed to delete file %s: %s\n", 
                    path.c_str(), delete_result.message.c_str());
            // return delete_result;
        }
        fprintf(stderr, "[delete_folder] Successfully deleted: %s\n", path.c_str());
    }

    for (const auto& path : folders_to_delete) {
        StorageResult delete_result;
        delete_result = delete_file(username, path, tablet_address);
        if (!delete_result.success) {
            fprintf(stderr, "[delete_folder] Failed to delete folder %s: %s\n", 
                    path.c_str(), delete_result.message.c_str());
            // return delete_result;
        }
        fprintf(stderr, "[delete_folder] Successfully deleted folder: %s\n", path.c_str());
    }
    
    auto self_delete_result = delete_file(username, normalized_path, tablet_address);
    if (!self_delete_result.success) {
        fprintf(stderr, "[delete_folder] Failed to delete folder %s: %s\n", 
                normalized_path.c_str(), self_delete_result.message.c_str());
        return self_delete_result;
    }
    
    fprintf(stderr, "[delete_folder] Successfully deleted folder: %s\n", normalized_path.c_str());
    return {true, "Folder and all contents deleted successfully", {}};
}

StorageResult move_item(const std::string& username,
                        const std::string& source_path,
                        const std::string& target_path,
                        const std::string& type,
                        const std::string& tablet_address) {
    std::stringstream cmd;
    std::string actual_source_path = source_path;
    std::string actual_target_path = target_path;
    
    if (type == "folder") {
        actual_source_path = source_path + "/";
        actual_target_path = target_path + "/";
        
        std::vector<FileEntry> all_files = list_files(username, tablet_address);
        std::vector<std::pair<std::string, std::string>> moves_to_perform;
        
        for (const auto& file : all_files) {
            if (file.path.find(actual_source_path) == 0) {
                std::string new_path = actual_target_path + file.path.substr(actual_source_path.length());
                if (file.type == "folder") {
                    moves_to_perform.push_back({file.path + "/", new_path + "/"});
                } else {
                    moves_to_perform.push_back({file.path, new_path});
                }
            }
        }

        moves_to_perform.push_back({actual_source_path, actual_target_path});
        
        for (const auto& [old_path, new_path] : moves_to_perform) {
            cmd.str("");
            cmd << "GET " << username << " " << old_path;
            
            auto [response, success] = Utils::tablet_command(tablet_address, cmd.str());
            if (response.substr(0, 4) != "+OK ") {
                return {false, "Failed to get item metadata: " + response, {}};
            }
            
            std::string metadata_str = response.substr(4);
            
            cmd.str("");
            cmd << "PUT " << username << " " << new_path << " " << metadata_str;
            auto [put_response, put_success] = Utils::tablet_command(tablet_address, cmd.str());
            
            if (put_response.substr(0, 4) != "+OK ") {
                return {false, "Failed to move item: " + put_response, {}};
            }
            
            cmd.str("");
            cmd << "DELETE " << username << " " << old_path;
            auto [delete_response, delete_success] = Utils::tablet_command(tablet_address, cmd.str());
            
            if (delete_response.substr(0, 4) != "+OK ") {
                return {false, "Failed to delete old item: " + delete_response, {}};
            }

            fprintf(stderr, "[move_item] Rec moved [%s] to [%s]\n", old_path.c_str(), new_path.c_str());
        }
        
        return {true, "Folder and all contents moved successfully", {}};
    } else {
        cmd << "GET " << username << " " << actual_source_path;
        
        auto [response, success] = Utils::tablet_command(tablet_address, cmd.str());
        if (response.substr(0, 4) != "+OK ") {
            return {false, "Item not found: " + response, {}};
        }

        std::string metadata_str = response.substr(4);
        
        cmd.str("");
        cmd << "PUT " << username << " " << actual_target_path << " " << metadata_str;
        auto [put_response, put_success] = Utils::tablet_command(tablet_address, cmd.str());
        
        if (put_response.substr(0, 4) != "+OK ") {
            return {false, "Failed to move item: " + put_response, {}};
        }
        
        cmd.str("");
        cmd << "DELETE " << username << " " << actual_source_path;
        auto [delete_response, delete_success] = Utils::tablet_command(tablet_address, cmd.str());

        fprintf(stderr, "[move_item] Moved [%s] to [%s]\n", actual_source_path.c_str(), actual_target_path.c_str());
        
        return process_tablet_response(delete_response, "Item moved successfully");
    }
}

StorageResult rename_item(const std::string& username,
                          const std::string& old_path,
                          const std::string& new_path,
                          const std::string& type,
                          const std::string& tablet_address) {
    return move_item(username, old_path, new_path, type, tablet_address);
}

} // namespace WebStorage


