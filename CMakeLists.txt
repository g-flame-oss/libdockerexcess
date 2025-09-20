cmake_minimum_required(VERSION 3.12)
project(docker-excess 
    VERSION 2.0.0
    DESCRIPTION "A robust, low-level C client for the Docker Engine API"
    LANGUAGES C)

# Set C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Options
option(DOCKER_EXCESS_BUILD_SHARED "Build shared library" ON)
option(DOCKER_EXCESS_BUILD_STATIC "Build static library" ON)
option(DOCKER_EXCESS_BUILD_EXAMPLES "Build example programs" ON)
option(DOCKER_EXCESS_BUILD_TESTS "Build test programs" ON)
option(DOCKER_EXCESS_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(DOCKER_EXCESS_ENABLE_TSAN "Enable ThreadSanitizer" OFF)

# Find required packages
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

# Find libcurl
find_package(CURL REQUIRED)
if(CURL_FOUND)
    message(STATUS "Found libcurl: ${CURL_VERSION_STRING}")
else()
    message(FATAL_ERROR "libcurl not found")
endif()

# Find json-c
pkg_check_modules(JSON_C REQUIRED json-c>=0.12)
if(JSON_C_FOUND)
    message(STATUS "Found json-c: ${JSON_C_VERSION}")
else()
    message(FATAL_ERROR "json-c not found")
endif()

# Compiler flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wpedantic")
set(CMAKE_C_FLAGS_DEBUG "-g -O0 -DDEBUG")
set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG")

# AddressSanitizer
if(DOCKER_EXCESS_ENABLE_ASAN)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
endif()

# ThreadSanitizer
if(DOCKER_EXCESS_ENABLE_TSAN)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=thread")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=thread")
endif()

# Source files
set(DOCKER_EXCESS_SOURCES
    src/docker-excess.c
    src/docker-excess-container.c
    src/docker-excess-image.c
    src/docker-excess-network.c
    src/docker-excess-volume.c
    src/docker-excess-file.c
    src/docker-excess-utils.c
)

set(DOCKER_EXCESS_HEADERS
    include/docker-excess.h
)

# Include directories
include_directories(include)
include_directories(${CURL_INCLUDE_DIRS})
include_directories(${JSON_C_INCLUDE_DIRS})

# Create shared library
if(DOCKER_EXCESS_BUILD_SHARED)
    add_library(docker-excess-shared SHARED ${DOCKER_EXCESS_SOURCES})
    set_target_properties(docker-excess-shared PROPERTIES
        OUTPUT_NAME docker-excess
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
        PUBLIC_HEADER "${DOCKER_EXCESS_HEADERS}"
    )
    
    target_link_libraries(docker-excess-shared
        ${CURL_LIBRARIES}
        ${JSON_C_LIBRARIES}
        Threads::Threads
        m
    )
    
    target_compile_definitions(docker-excess-shared PRIVATE
        DOCKER_EXCESS_BUILD_SHARED=1
    )
    
    # Install shared library
    install(TARGETS docker-excess-shared
        LIBRARY DESTINATION lib
        PUBLIC_HEADER DESTINATION include
    )
endif()

# Create static library
if(DOCKER_EXCESS_BUILD_STATIC)
    add_library(docker-excess-static STATIC ${DOCKER_EXCESS_SOURCES})
    set_target_properties(docker-excess-static PROPERTIES
        OUTPUT_NAME docker-excess
        PUBLIC_HEADER "${DOCKER_EXCESS_HEADERS}"
    )
    
    target_link_libraries(docker-excess-static
        ${CURL_LIBRARIES}
        ${JSON_C_LIBRARIES}
        Threads::Threads
        m
    )
    
    target_compile_definitions(docker-excess-static PRIVATE
        DOCKER_EXCESS_BUILD_STATIC=1
    )
    
    # Install static library
    install(TARGETS docker-excess-static
        ARCHIVE DESTINATION lib
        PUBLIC_HEADER DESTINATION include
    )
endif()

# Create alias for easier linking
if(DOCKER_EXCESS_BUILD_SHARED)
    add_library(docker-excess::docker-excess ALIAS docker-excess-shared)
elseif(DOCKER_EXCESS_BUILD_STATIC)
    add_library(docker-excess::docker-excess ALIAS docker-excess-static)
endif()

# Examples
if(DOCKER_EXCESS_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Tests
if(DOCKER_EXCESS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# pkg-config file
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/docker-excess.pc.in
    ${CMAKE_CURRENT_BINARY_DIR}/docker-excess.pc
    @ONLY
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/docker-excess.pc
    DESTINATION lib/pkgconfig
)

# Uninstall target
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/cmake_uninstall.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake
    IMMEDIATE @ONLY
)

add_custom_target(uninstall
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake
)

# Documentation (if Doxygen is available)
find_package(Doxygen)
if(DOXYGEN_FOUND)
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in
        ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile @ONLY)
    
    add_custom_target(doc
        ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Generating API documentation with Doxygen" VERBATIM
    )
endif()

# Package configuration
set(CPACK_PACKAGE_NAME "docker-excess")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_VENDOR "G-flame-OSS")
set(CPACK_PACKAGE_CONTACT "developer@example.com")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")

# DEB package configuration
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libcurl4, libjson-c4")
set(CPACK_DEBIAN_PACKAGE_SECTION "devel")

# RPM package configuration
set(CPACK_RPM_PACKAGE_REQUIRES "libcurl, json-c")
set(CPACK_RPM_PACKAGE_GROUP "Development/Libraries")

include(CPack)

# Print configuration summary
message(STATUS "")
message(STATUS "Docker-excess build configuration:")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  C compiler: ${CMAKE_C_COMPILER}")
message(STATUS "  C flags: ${CMAKE_C_FLAGS}")
message(STATUS "  Build shared: ${DOCKER_EXCESS_BUILD_SHARED}")
message(STATUS "  Build static: ${DOCKER_EXCESS_BUILD_STATIC}")
message(STATUS "  Build examples: ${DOCKER_EXCESS_BUILD_EXAMPLES}")
message(STATUS "  Build tests: ${DOCKER_EXCESS_BUILD_TESTS}")
message(STATUS "  Enable ASAN: ${DOCKER_EXCESS_ENABLE_ASAN}")
message(STATUS "  Enable TSAN: ${DOCKER_EXCESS_ENABLE_TSAN}")
message(STATUS "  libcurl: ${CURL_VERSION_STRING}")
message(STATUS "  json-c: ${JSON_C_VERSION}")
message(STATUS "")
