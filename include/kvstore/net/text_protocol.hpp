#ifndef KVSTORE_NET_TEXT_PROTOCOL_HPP
#define KVSTORE_NET_TEXT_PROTOCOL_HPP

#include <string>
#include <vector>

#include "kvstore/net/types.hpp"

namespace kvstore::net {

class TextProtocol {
   public:
    // Encode
    static std::string encode_request(const Request& req);
    static std::string encode_response(const Response& resp);

    // Decode
    static Request decode_request(const std::string& line);
    static Response decode_response(const std::string& line);

    // Conversions
    static std::string command_to_string(Command cmd);
    static Command parse_command(const std::string& str);
};

}  // namespace kvstore::net

#endif