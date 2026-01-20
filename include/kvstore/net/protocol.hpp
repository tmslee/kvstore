#ifndef KVSTORE_NET_PROTOCOL_HPP
#define KVSTORE_NET_PROTOCOL_HPP

#include <string>
#include <vector>

namespace kvstore::net {

enum class StatusCode {
    Ok,
    NotFound,
    Error,
    Bye,
};

struct CommandResult {
    StatusCode status;
    std::string message;
    bool close_connection = false;
};

struct ParsedCommand {
    std::string command;
    std::vector<std::string> args;
};

class Protocol {
   public:
    // static functions - no per-instance data (no member variables)
    // essentially namespaced functions that live inside a class
    [[nodiscard]] static ParsedCommand parse(const std::string& line);
    [[nodiscard]] static std::string serialize(const CommandResult& result);

    [[nodiscard]] static CommandResult ok();
    [[nodiscard]] static CommandResult ok(const std::string& message);
    [[nodiscard]] static CommandResult not_found();
    [[nodiscard]] static CommandResult error(const std::string& message);
    [[nodiscard]] static CommandResult bye();
};

}  // namespace kvstore::net

#endif