#include "imap_client.h"

#include <sstream>
#include <stdexcept>

#include <curl/curl.h>

namespace watcher {

namespace {

size_t write_to_string(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

struct CurlHandle {
    CURL* handle;
    CurlHandle() : handle(curl_easy_init()) {
        if (!handle) throw std::runtime_error("failed to initialize curl");
    }
    ~CurlHandle() { curl_easy_cleanup(handle); }
};

void set_common_options(CURL* curl, const ImapConfig& config, const std::string& url) {
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERNAME, config.username.c_str());
    curl_easy_setopt(curl, CURLOPT_PASSWORD, config.password.c_str());
    curl_easy_setopt(curl, CURLOPT_USE_SSL, (long)CURLUSESSL_ALL);
    curl_easy_setopt(curl, CURLOPT_PORT, (long)config.port);
}

}  // namespace

ImapClient::ImapClient(ImapConfig config) : config_(std::move(config)) {}

std::vector<std::int64_t> ImapClient::search_uids_after(std::int64_t last_uid) {
    CurlHandle curl;
    std::string response;

    std::string url = "imaps://" + config_.host + "/INBOX";
    set_common_options(curl.handle, config_, url);

    std::string command = (last_uid <= 0)
        ? "UID SEARCH ALL"
        : "UID SEARCH UID " + std::to_string(last_uid + 1) + ":*";
    curl_easy_setopt(curl.handle, CURLOPT_CUSTOMREQUEST, command.c_str());
    curl_easy_setopt(curl.handle, CURLOPT_WRITEFUNCTION, write_to_string);
    curl_easy_setopt(curl.handle, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl.handle);
    if (res != CURLE_OK) {
        throw std::runtime_error(std::string("IMAP SEARCH failed: ") + curl_easy_strerror(res));
    }

    std::vector<std::int64_t> uids;
    std::istringstream stream(response);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.find("* SEARCH") != 0) continue;
        std::istringstream tokens(line.substr(8));
        std::int64_t uid;
        while (tokens >> uid) uids.push_back(uid);
    }
    return uids;
}

std::string ImapClient::fetch_message(std::int64_t uid) {
    CurlHandle curl;
    std::string response;

    std::string url = "imaps://" + config_.host + "/INBOX;UID=" + std::to_string(uid);
    set_common_options(curl.handle, config_, url);
    curl_easy_setopt(curl.handle, CURLOPT_WRITEFUNCTION, write_to_string);
    curl_easy_setopt(curl.handle, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl.handle);
    if (res != CURLE_OK) {
        throw std::runtime_error(std::string("IMAP FETCH failed: ") + curl_easy_strerror(res));
    }
    return response;
}

}  // namespace watcher
