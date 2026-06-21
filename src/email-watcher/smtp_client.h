#pragma once

#include <string>

namespace watcher {

struct SmtpConfig {
    std::string host;
    int port = 465;
    std::string username;
    std::string password;
};

// raw_message must be a complete RFC822 message (headers + body), as
// produced by mime::build_message.
void send_message(const SmtpConfig& config, const std::string& from,
                   const std::string& to, const std::string& raw_message);

}  // namespace watcher
