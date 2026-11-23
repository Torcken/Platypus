#ifndef PLATYPUS_UTIL_STRING_HPP
#define PLATYPUS_UTIL_STRING_HPP

#include <string>
#include <vector>
#include <algorithm>
#include <sstream>

namespace platypus::util {

class StringUtils {
public:
    static std::string trim(const std::string& s) {
        auto first = s.find_first_not_of(" \t\n\r");
        if (std::string::npos == first) return "";
        auto last = s.find_last_not_of(" \t\n\r");
        return s.substr(first, (last - first + 1));
    }

    static std::string join(const std::vector<std::string>& elements, const std::string& delimiter) {
        std::stringstream ss;
        for (size_t i = 0; i < elements.size(); ++i) {
            ss << elements[i];
            if (i < elements.size() - 1) ss << delimiter;
        }
        return ss.str();
    }

    static std::string to_lower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), 
                       [](unsigned char c){ return std::tolower(c); });
        return s;
    }
};

} // namespace platypus::util

#endif // PLATYPUS_UTIL_STRING_HPP
