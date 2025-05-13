#ifndef WEBSTORAGE_HPP
#define WEBSTORAGE_HPP

#include <string>
#include <vector>
#include "include/webstorage/webstorage_file.hpp"

namespace WebStorage {
    struct FileEntry {
        std::string path;      // Full path including filename
        std::string name;      // Just the filename/foldername
        std::string type;      // "file" or "folder"
        File::Metadata metadata;
        
        std::string to_string() const {
            return path + ":" + type;
        }
        
        static FileEntry from_string(const std::string& str) {
            FileEntry entry;
            size_t colon_pos = str.find(':');
            if (colon_pos != std::string::npos) {
                entry.path = str.substr(0, colon_pos);
                entry.type = str.substr(colon_pos + 1);
                size_t last_slash = entry.path.find_last_of('/');
                entry.name = (last_slash != std::string::npos) ? 
                            entry.path.substr(last_slash + 1) : entry.path;
            }
            return entry;
        }
    };

    struct StorageResult {
        bool success;
        std::string message;
        std::vector<char> data;
    };

    StorageResult upload_file_chunk(const std::string& username,
                              const std::string& filename,
                              const std::vector<char>& chunk_data,
                              int part,
                              int total_parts,
                              const std::string& tablet_address,
                              const std::string& content_type);

    StorageResult download_file_chunk(const std::string& username,
                                      const std::string& filename,
                                      int part,
                                      const std::string& tablet_address);

    bool file_exists(const std::string& username, 
                     const std::string& filename,
                     const std::string& tablet_address);

    std::vector<FileEntry> list_files(const std::string& username,
                                      const std::string& tablet_address);

    StorageResult delete_file(const std::string& username,
                              const std::string& filename,
                              const std::string& tablet_address);

    StorageResult create_folder(const std::string& username,
                                const std::string& folder_path,
                                const std::string& tablet_address);

    StorageResult delete_folder(const std::string& username,
                                const std::string& folder_path,
                                const std::string& tablet_address);

    StorageResult move_item(const std::string& username,
                            const std::string& source_path,
                            const std::string& target_path,
                            const std::string& type,
                            const std::string& tablet_address);

    StorageResult rename_item(const std::string& username,
                              const std::string& old_path,
                              const std::string& new_path,
                              const std::string& type,
                              const std::string& tablet_address);
}

#endif


