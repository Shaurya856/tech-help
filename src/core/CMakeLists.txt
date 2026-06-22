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
if(TARGET unofficial::libharu::hpdf)
  set(LIBHARU_TARGET unofficial::libharu::hpdf)
elseif(TARGET libharu::hpdf)
  set(LIBHARU_TARGET libharu::hpdf)
else()
  message(FATAL_ERROR "libharu imported target not found. Inspect vcpkg_installed/.../share/unofficial-libharu/ for the exported target name.")
endif()

if(TARGET qpdf::libqpdf)
  set(QPDF_TARGET qpdf::libqpdf)
elseif(TARGET qpdf::qpdf)
  set(QPDF_TARGET qpdf::qpdf)
elseif(TARGET libqpdf)
  set(QPDF_TARGET libqpdf)
else()
  message(FATAL_ERROR "qpdf imported target not found. Inspect vcpkg_installed/.../share/qpdf/ for the exported target name.")
endif()

if(TARGET libheif::heif)
  set(LIBHEIF_TARGET libheif::heif)
elseif(TARGET libheif::libheif)
  set(LIBHEIF_TARGET libheif::libheif)
elseif(TARGET libheif)
  set(LIBHEIF_TARGET libheif)
else()
  message(FATAL_ERROR "libheif imported target not found. Inspect vcpkg_installed/.../share/libheif/ for the exported target name.")
endif()

message(STATUS "core linking: ${LIBHARU_TARGET}  ${QPDF_TARGET}  TIFF::TIFF  ${LIBHEIF_TARGET}")

target_link_libraries(core PUBLIC nlohmann_json::nlohmann_json)
target_link_libraries(core PRIVATE ${LIBHARU_TARGET} ${QPDF_TARGET} TIFF::TIFF ${LIBHEIF_TARGET})
