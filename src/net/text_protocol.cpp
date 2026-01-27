#include "kvstore/net/text_protocol.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace kvstore::net {

namespace {
std::string to_upper(std::string str) {
    std::transform(str.begin(), str.end(), str.begin(), ::toupper);
    return str;
}

}  // namespace

std::string TextProtocol::encode_request(const Request& req) {
    std::string line = command_to_string(req.command);

    switch (req.command) {
        case Command::Get:
        case Command::Del:
        case Command::Exists:
            line += " " + req.key;
            break;

        case Command::Put:
            line += " " + req.key + " " + req.value;
            break;

        case Command::PutEx:
            line += " " + req.key + " " + std::to_string(req.ttl_ms) + " " + req.value;
            break;

        default:
            break;
    }

    line += "\n";
    return line;
}

std::string TextProtocol::encode_response(const Response& resp) {
    std::string line;

    switch (resp.status) {
        case Status::Ok:
            line = resp.data.empty() ? "OK" : "OK " + resp.data;
            break;
        case Status::NotFound:
            line = "NOT_FOUND";
            break;
        case Status::Error:
            line = "ERROR " + resp.data;
            break;
        case Status::Bye:
            line = "BYE";
            break;
    }

    line += "\n";
    return line;
}

Request TextProtocol::decode_request(const std::string& line) {
    std::istringstream iss(line);
    std::string cmd_str;

    if (!(iss >> cmd_str)) {
        return {Command::Unknown, "", "", 0};
    }

    Request req;
    req.command = parse_command(cmd_str);

    std::vector<std::string> args;
    std::string arg;
    while (iss >> arg) {
        args.push_back(arg);
    }

    switch (req.command) {
        case Command::Get:
        case Command::Del:
        case Command::Exists:
            if(args.empty()) {
                req.command = Command::Unknown;
            } else {
                req.key = args[0];
            }
            break;

        case Command::Put:
            if(args.size() < 2) {
                req.command = Command::Unknown;
            } else {
                req.key = args[0];
                for (size_t i = 1; i < args.size(); ++i) {
                    if (i > 1)
                        req.value += " ";
                    req.value += args[i];
                }
            }
            break;

        case Command::PutEx:
            if(args.size() < 3) {
                req.command = Command::Unknown;
            } else {
                req.key = args[0];
                try {
                    req.ttl_ms = std::stoll(args[1]);
                } catch (const std::exception&) {
                    req.command = Command::Unknown; //invalid TTL
                    break;
                }
                for (size_t i = 2; i < args.size(); ++i) {
                    if (i > 2)
                        req.value += " ";
                    req.value += args[i];
                }
            }
            break;

        default:
            break;
    }

    return req;
}

Response TextProtocol::decode_response(const std::string& line) {
    Response resp;

    if (line.substr(0, 2) == "OK") {
        resp.status = Status::Ok;
        if (line.size() > 3) {
            resp.data = line.substr(3);
        }
    } else if (line == "NOT_FOUND") {
        resp.status = Status::NotFound;
    } else if (line.substr(0, 5) == "ERROR") {
        resp.status = Status::Error;
        if (line.size() > 6) {
            resp.data = line.substr(6);
        }
    } else if (line == "BYE") {
        resp.status = Status::Bye;
        resp.close_connection = true;
    } else {
        resp.status = Status::Error;
        resp.data = "Unknown response: " + line;
    }

    return resp;
}

std::string TextProtocol::command_to_string(Command cmd) {
    switch (cmd) {
        case Command::Get:
            return "GET";
        case Command::Put:
            return "PUT";
        case Command::PutEx:
            return "PUTEX";
        case Command::Del:
            return "DEL";
        case Command::Exists:
            return "EXISTS";
        case Command::Size:
            return "SIZE";
        case Command::Clear:
            return "CLEAR";
        case Command::Ping:
            return "PING";
        case Command::Quit:
            return "QUIT";
        case Command::Unknown:
            return "UNKNOWN";
    }
    return "UNKNOWN";
}

Command TextProtocol::parse_command(const std::string& str) {
    std::string upper = to_upper(str);
    if (upper == "GET")
        return Command::Get;
    if (upper == "PUT" || upper == "SET")
        return Command::Put;
    if (upper == "PUTEX" || upper == "SETEX")
        return Command::PutEx;
    if (upper == "DEL" || upper == "DELETE" || upper == "REMOVE")
        return Command::Del;
    if (upper == "EXISTS" || upper == "CONTAINS")
        return Command::Exists;
    if (upper == "SIZE" || upper == "COUNT")
        return Command::Size;
    if (upper == "CLEAR")
        return Command::Clear;
    if (upper == "PING")
        return Command::Ping;
    if (upper == "QUIT" || upper == "EXIT")
        return Command::Quit;
    return Command::Unknown;
}

}  // namespace kvstore::net