find_package(unofficial-libharu CONFIG REQUIRED)
find_package(qpdf CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_library(core STATIC
    process_file.cpp
    config.cpp
    strings.cpp
)

target_include_directories(core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(core PUBLIC nlohmann_json::nlohmann_json)
target_link_libraries(core PRIVATE unofficial::libharu::hpdf qpdf::libqpdf)
