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
    std::vector<Attachment> attachments;
};

std::string base64_encode(const std::vector<std::uint8_t>& data);
std::vector<std::uint8_t> base64_decode(const std::string& encoded);

// Extracts attachments (parts with a filename) from a raw RFC822 message.
// Handles single-part and multipart/* messages with base64-encoded parts.
// Best-effort: malformed or unrecognized parts are skipped, not fatal.
ParsedMessage parse_message(const std::string& raw_message);

// Builds a raw RFC822 message: a plain-text body plus zero or more
// attachments, as multipart/mixed if there are attachments.
std::string build_message(const std::string& from, const std::string& to,
                           const std::string& subject, const std::string& text_body,
                           const std::vector<Attachment>& attachments);

}  // namespace mime
