#ifndef WEBSTORAGE_FILE_HPP
#define WEBSTORAGE_FILE_HPP

#include <string>
#include <vector>

namespace File {
    struct Metadata {
        std::string type;
        int part;
        int total;
        std::string timestamp;
        
        std::string to_string() const;
        static Metadata from_string(const std::string& str);
    };

    std::string timestamp();
    std::string create_file_metadata(const int& part, const int& total, const std::string& content_type);
    std::string create_folder_metadata();
    std::string normalize_path(const std::string& path);
    std::string get_file_key(const std::string& current_path, const std::string& filename);
    std::string get_folder_key(const std::string& current_path, const std::string& foldername);
}

#endif

