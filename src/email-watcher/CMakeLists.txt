find_package(CURL REQUIRED)

add_executable(email-watcher
    main.cpp
    mime.cpp
    state.cpp
    imap_client.cpp
    smtp_client.cpp
    print_job.cpp
)
target_link_libraries(email-watcher PRIVATE core CURL::libcurl Winspool.lib)
target_compile_definitions(email-watcher PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
