#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mime {

struct Attachment {
    std::string filename;
    std::string content_type;
    std::vector<std::uint8_t> data;
};

struct ParsedMessage {
    std::string from_address;
    std::string subject;
    std::string message_id;
    std::string in_reply_to;
    std::string references;
    std::string body_text;
    std::vector<Attachment> attachments;
};

std::string base64_encode(const std::vector<std::uint8_t>& data);
std::vector<std::uint8_t> base64_decode(const std::string& encoded);

// Extracts attachments and plain-text body from a raw RFC822 message.
ParsedMessage parse_message(const std::string& raw_message);

// Builds a raw RFC822 message. If message_id is non-empty it is set as the
// outgoing Message-ID header. If in_reply_to is non-empty, an In-Reply-To
// header is added (used for print-confirmation threading).
std::string build_message(const std::string& from, const std::string& to,
                           const std::string& subject, const std::string& text_body,
                           const std::vector<Attachment>& attachments,
                           const std::string& message_id = "",
                           const std::string& in_reply_to = "");

}  // namespace mime
