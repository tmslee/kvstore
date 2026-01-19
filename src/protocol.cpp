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
    ParsedCommand result;
    std::istringstream iss(trim(line));

    std::string token;
    if(iss >> token) {
        result.command = to_upper(token);
    }
    while(iss >> token) {
        result.args.push_back(token);
    }
    return result;
}

std::string Protocol::serialize(const CommandResult& result) {
    std::string output;
    switch(result.status) {
        case StatusCode::Ok:
            output = "OK";
            break;
        case StatusCode::NotFound:
            output = "NOT_FOUND";
            break;
        case StatusCode::Error:
            output = "ERROR";
            break;
        case StatusCode::Bye:
            output = "BYE";
            break;
    }
    if(!result.message.empty()) {
        output += " " + result.message; 
    }
    output += "\n";
    return output;
}

CommandResult Protocol::ok() {
    return {StatusCode::Ok, "", false};
}

CommandResult Protocol::ok(const std::string& message){
    return {StatusCode::Ok, message, false};
}

CommandResult Protocol::not_found() {
    return {StatusCode::NotFound, "", false};
}

CommandResult Protocol::error(const std::string& message) {
    return {StatusCode::Error, message, false};
}

CommandResult Protocol::bye() {
    return {StatusCode::Bye, "", true};
}

} //namespace kvstore