add_executable(ui WIN32 main.cpp)
target_link_libraries(ui PRIVATE core comdlg32 shell32)
target_compile_definitions(ui PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
