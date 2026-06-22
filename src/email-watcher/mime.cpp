#include "mime.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <utility>

namespace mime {

namespace {

const char kBase64Chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

std::pair<std::string, std::string> split_headers_body(const std::string& msg) {
    size_t pos = msg.find("\r\n\r\n");
    size_t sep_len = 4;
    if (pos == std::string::npos) {
        pos = msg.find("\n\n");
        sep_len = 2;
    }
    if (pos == std::string::npos) return {msg, ""};
    return {msg.substr(0, pos), msg.substr(pos + sep_len)};
}

std::vector<std::pair<std::string, std::string>> parse_headers(const std::string& block) {
    std::vector<std::pair<std::string, std::string>> headers;
    std::istringstream stream(block);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if ((line[0] == ' ' || line[0] == '\t') && !headers.empty()) {
            headers.back().second += " " + trim(line);
            continue;
        }
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        headers.emplace_back(trim(line.substr(0, colon)), trim(line.substr(colon + 1)));
    }
    return headers;
}

std::string header_value(const std::vector<std::pair<std::string, std::string>>& headers,
                          const std::string& name) {
    std::string lname = to_lower(name);
    for (auto& [k, v] : headers) {
        if (to_lower(k) == lname) return v;
    }
    return "";
}

std::string header_param(const std::string& header_value_str, const std::string& param) {
    std::string lname = to_lower(param) + "=";
    std::string lower_value = to_lower(header_value_str);
    size_t pos = lower_value.find(lname);
    if (pos == std::string::npos) return "";
    pos += lname.size();
    if (pos >= header_value_str.size()) return "";
    if (header_value_str[pos] == '"') {
        size_t end = header_value_str.find('"', pos + 1);
        if (end == std::string::npos) return "";
        return header_value_str.substr(pos + 1, end - pos - 1);
    }
    size_t end = header_value_str.find_first_of(";\r\n", pos);
    return trim(header_value_str.substr(pos, end == std::string::npos ? std::string::npos : end - pos));
}

void parse_part(const std::string& part, ParsedMessage& out) {
    auto [header_block, body] = split_headers_body(part);
    auto headers = parse_headers(header_block);

    std::string content_type = header_value(headers, "Content-Type");
    std::string lower_ct = to_lower(content_type);

    if (lower_ct.find("multipart/") == 0) {
        std::string boundary = header_param(content_type, "boundary");
        if (boundary.empty()) return;
        std::string delimiter = "--" + boundary;
        size_t pos = body.find(delimiter);
        while (pos != std::string::npos) {
            size_t part_start = pos + delimiter.size();
            size_t next = body.find(delimiter, part_start);
            if (next == std::string::npos) break;
            std::string sub_part = body.substr(part_start, next - part_start);
            size_t content_start = sub_part.find_first_not_of("\r\n");
            if (content_start != std::string::npos) {
                parse_part(sub_part.substr(content_start), out);
            }
            pos = next;
        }
        return;
    }

    std::string disposition = header_value(headers, "Content-Disposition");
    std::string filename = header_param(disposition, "filename");
    if (filename.empty()) filename = header_param(content_type, "name");

    std::string encoding = to_lower(header_value(headers, "Content-Transfer-Encoding"));

    // No filename → this is a body part.
    if (filename.empty()) {
        if (lower_ct.find("text/plain") == 0 && out.body_text.empty()) {
            if (encoding.find("base64") != std::string::npos) {
                auto decoded = base64_decode(body);
                out.body_text.assign(decoded.begin(), decoded.end());
            } else {
                out.body_text = trim(body);
            }
        }
        return;
    }

    Attachment att;
    att.filename = filename;
    att.content_type = content_type;
    if (encoding.find("base64") != std::string::npos) {
        att.data = base64_decode(body);
    } else {
        att.data.assign(body.begin(), body.end());
    }
    out.attachments.push_back(std::move(att));
}

}  // namespace

std::string base64_encode(const std::vector<std::uint8_t>& data) {
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    size_t i = 0;
    while (i + 3 <= data.size()) {
        std::uint32_t n = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
        out += kBase64Chars[(n >> 18) & 0x3F];
        out += kBase64Chars[(n >> 12) & 0x3F];
        out += kBase64Chars[(n >> 6) & 0x3F];
        out += kBase64Chars[n & 0x3F];
        i += 3;
    }
    size_t remaining = data.size() - i;
    if (remaining == 1) {
        std::uint32_t n = data[i] << 16;
        out += kBase64Chars[(n >> 18) & 0x3F];
        out += kBase64Chars[(n >> 12) & 0x3F];
        out += "==";
    } else if (remaining == 2) {
        std::uint32_t n = (data[i] << 16) | (data[i + 1] << 8);
        out += kBase64Chars[(n >> 18) & 0x3F];
        out += kBase64Chars[(n >> 12) & 0x3F];
        out += kBase64Chars[(n >> 6) & 0x3F];
        out += "=";
    }
    return out;
}

std::vector<std::uint8_t> base64_decode(const std::string& encoded) {
    auto decode_char = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };

    std::vector<std::uint8_t> out;
    int buffer = 0;
    int bits_collected = 0;
    for (char c : encoded) {
        if (c == '=' || c == '\r' || c == '\n') continue;
        int value = decode_char(c);
        if (value < 0) continue;
        buffer = (buffer << 6) | value;
        bits_collected += 6;
        if (bits_collected >= 8) {
            bits_collected -= 8;
            out.push_back(static_cast<std::uint8_t>((buffer >> bits_collected) & 0xFF));
        }
    }
    return out;
}

ParsedMessage parse_message(const std::string& raw_message) {
    ParsedMessage result;
    auto [header_block, body] = split_headers_body(raw_message);
    (void)body;
    auto headers = parse_headers(header_block);
    result.from_address = header_value(headers, "From");
    result.subject      = header_value(headers, "Subject");
    result.message_id   = header_value(headers, "Message-ID");
    result.in_reply_to  = header_value(headers, "In-Reply-To");
    result.references   = header_value(headers, "References");

    parse_part(raw_message, result);
    return result;
}

std::string build_message(const std::string& from, const std::string& to,
                           const std::string& subject, const std::string& text_body,
                           const std::vector<Attachment>& attachments,
                           const std::string& message_id,
                           const std::string& in_reply_to) {
    const std::string boundary = "tech-help-boundary-7f3a9c";
    std::ostringstream out;
    out << "From: " << from << "\r\n"
        << "To: " << to << "\r\n"
        << "Subject: " << subject << "\r\n"
        << "MIME-Version: 1.0\r\n";

    if (!message_id.empty()) {
        out << "Message-ID: " << message_id << "\r\n";
    }
    if (!in_reply_to.empty()) {
        out << "In-Reply-To: " << in_reply_to << "\r\n"
            << "References: " << in_reply_to << "\r\n";
    }

    if (attachments.empty()) {
        out << "Content-Type: text/plain; charset=utf-8\r\n\r\n" << text_body << "\r\n";
        return out.str();
    }

    out << "Content-Type: multipart/mixed; boundary=\"" << boundary << "\"\r\n\r\n";
    out << "--" << boundary << "\r\n"
        << "Content-Type: text/plain; charset=utf-8\r\n\r\n"
        << text_body << "\r\n";

    for (const auto& att : attachments) {
        out << "--" << boundary << "\r\n"
            << "Content-Type: "
            << (att.content_type.empty() ? "application/octet-stream" : att.content_type) << "\r\n"
            << "Content-Disposition: attachment; filename=\"" << att.filename << "\"\r\n"
            << "Content-Transfer-Encoding: base64\r\n\r\n"
            << base64_encode(att.data) << "\r\n";
    }
    out << "--" << boundary << "--\r\n";
    return out.str();
}

}  // namespace mime
