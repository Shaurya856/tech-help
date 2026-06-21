find_package(CURL REQUIRED)

add_executable(email-watcher
    main.cpp
    mime.cpp
    state.cpp
    imap_client.cpp
    smtp_client.cpp
)
target_link_libraries(email-watcher PRIVATE core CURL::libcurl)
