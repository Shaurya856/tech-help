find_package(CURL REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)

add_executable(email-watcher
    main.cpp
    mime.cpp
    state.cpp
    imap_client.cpp
    smtp_client.cpp
    print_job.cpp
    log.cpp
)
target_link_libraries(email-watcher PRIVATE core CURL::libcurl Winspool.lib unofficial::sqlite3::sqlite3)
target_compile_definitions(email-watcher PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
