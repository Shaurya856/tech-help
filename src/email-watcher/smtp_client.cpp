#include "smtp_client.h"

#include <cstring>
#include <stdexcept>

#include <curl/curl.h>

namespace watcher {

namespace {

struct ReadContext {
    const std::string& data;
    size_t offset = 0;
};

size_t read_from_string(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* ctx = static_cast<ReadContext*>(userdata);
    size_t remaining = ctx->data.size() - ctx->offset;
    size_t to_copy = std::min(remaining, size * nmemb);
    std::memcpy(ptr, ctx->data.data() + ctx->offset, to_copy);
    ctx->offset += to_copy;
    return to_copy;
}

}  // namespace

void send_message(const SmtpConfig& config, const std::string& from,
                   const std::string& to, const std::string& raw_message) {
    CURL* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("failed to initialize curl");

    std::string url = "smtps://" + config.host + ":" + std::to_string(config.port);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERNAME, config.username.c_str());
    curl_easy_setopt(curl, CURLOPT_PASSWORD, config.password.c_str());
    curl_easy_setopt(curl, CURLOPT_USE_SSL, (long)CURLUSESSL_ALL);
    curl_easy_setopt(curl, CURLOPT_MAIL_FROM, from.c_str());

    curl_slist* recipients = curl_slist_append(nullptr, to.c_str());
    curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);

    ReadContext ctx{raw_message, 0};
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, read_from_string);
    curl_easy_setopt(curl, CURLOPT_READDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);

    CURLcode res = curl_easy_perform(curl);

    curl_slist_free_all(recipients);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        throw std::runtime_error(std::string("SMTP send failed: ") + curl_easy_strerror(res));
    }
}

}  // namespace watcher
