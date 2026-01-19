#include "kvstore/protocol.hpp"

#include <algorithm>
#include <sstream>

namespace kvstore {

namespace {
    std::string trim(const std::string& str) {
        const char* ws = " \t\r\n";
        auto start = str.find_first_not_of(ws);
        if (start == std::string::npos) {
            return "";
        }
        auto end = str.find_last_not_of(ws);
        return str.substr(start, end - start + 1);
    }

    std::string to_upper(std::string str) {
        std::transform(str.begin(), str.end(), str.begin(), ::toupper);
        return str;
    }

} //namespace

ParsedCommand Protocol::parse(const std::string& line) {

}

std::string Protocol::serialize(const CommandResult& result) {

}

CommandResult Protocol::ok() {

}

CommandResult Protocol::ok(const std::string& message){

}

CommandResult Protocol::not_found() {

}

CommandResult Protocol::error(const std::string& message) {

}

CommandResult Protocol::bye() {
    
}

} //namespace kvstore