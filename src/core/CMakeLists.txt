find_package(unofficial-libharu CONFIG REQUIRED)
find_package(qpdf CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(TIFF REQUIRED)
find_package(libheif CONFIG REQUIRED)
find_path(STB_INCLUDE_DIR NAMES "stb_image.h" REQUIRED)

add_library(core STATIC
    process_file.cpp
    image_compress.cpp
    stb_impl.cpp
    config.cpp
    strings.cpp
)

target_include_directories(core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(core PRIVATE ${STB_INCLUDE_DIR})
target_link_libraries(core PUBLIC nlohmann_json::nlohmann_json)
target_link_libraries(core PRIVATE unofficial::libharu::hpdf qpdf::libqpdf TIFF::TIFF libheif::heif)
