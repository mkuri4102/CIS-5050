#include "include/handlers/handler_utils.h"

namespace Routes {

bool ends_with(const std::string& str, const std::string& suffix) {
    if (str.length() < suffix.length()) {
        return false;
    }
    return str.compare(str.length() - suffix.length(), suffix.length(), suffix) == 0;
}

bool is_protected_route(const std::string& path) {
    for (const auto& route : PROTECTED_ROUTES) {
        if (path == route) {
            return true;
        }
    }
    return false;
}

} // namespace Routes 

