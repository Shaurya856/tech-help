set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)

# We only ship Release builds - skip building each dependency's debug
# config too, which otherwise roughly doubles dependency build time.
set(VCPKG_BUILD_TYPE release)
