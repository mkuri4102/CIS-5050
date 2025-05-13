#ifndef HANDLER_UTILS_H
#define HANDLER_UTILS_H

#include <string>
#include <vector>

namespace Routes {

// utility function to check if a string ends with a given suffix
bool ends_with(const std::string& str, const std::string& suffix);

// list of protected routes
const std::vector<std::string> PROTECTED_ROUTES = {
    "/storage",
    "/storage/",
    "/webmail",
    "/webmail/",
    "/profile",
    "/profile/",
    "/dashboard",
    "/dashboard/",
    "/admin/data",
    "/admin/data/"
};

// utility function to check if a given path is protected
bool is_protected_route(const std::string& path);

} // namespace Routes

#endif 

