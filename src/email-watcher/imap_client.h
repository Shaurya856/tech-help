#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace watcher {

struct ImapConfig {
    std::string host;
    int port = 993;
    std::string username;
    std::string password;
};

class ImapClient {
public:
    explicit ImapClient(ImapConfig config);

    // Returns UIDs of messages with UID > last_uid (ascending). If last_uid
    // is 0, performs a full UID SEARCH ALL (used only for first-run bootstrap).
    std::vector<std::int64_t> search_uids_after(std::int64_t last_uid);

    // Returns the raw RFC822 message for the given UID.
    std::string fetch_message(std::int64_t uid);

private:
    ImapConfig config_;
};

}  // namespace watcher
