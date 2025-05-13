#ifndef WEBSTORAGE_HANDLER_HPP
#define WEBSTORAGE_HANDLER_HPP

#include <string>

namespace WebStorageHandler {

std::string get_username_from_session(const char* buffer);

void handle_storage_request(int client_fd, const std::string& path, const char* buffer, bool keep_alive = false);

std::string get_current_path(const std::string& url_path);

std::string get_file_key(const std::string& current_path, const std::string& filename);

std::string get_folder_key(const std::string& current_path, const std::string& foldername);

}

#endif // WEBSTORAGE_HANDLER_HPP 


