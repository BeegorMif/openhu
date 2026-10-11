cmake_minimum_required(VERSION 3.14)

project(openauto CXX)

message(STATUS "OpenHU")

# Versioning: align with AASDK (YYYY.MM.DD[+git.<sha>[.dirty]][.debug])
# Date-based semantic versioning
string(TIMESTAMP OPENAUTO_BUILD_YEAR "%Y" UTC)
string(TIMESTAMP OPENAUTO_BUILD_MONTH "%m" UTC)
string(TIMESTAMP OPENAUTO_BUILD_DAY "%d" UTC)


set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Set version components
set(OPENAUTO_BUILD_MAJOR_RELEASE ${OPENAUTO_BUILD_YEAR})
set(OPENAUTO_BUILD_MINOR_RELEASE ${OPENAUTO_BUILD_MONTH})
set(OPENAUTO_BUILD_PATCH_RELEASE ${OPENAUTO_BUILD_DAY})

# Configure CMAKE
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS_INIT} -Wall -pedantic -fPIC")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "-g -O3")

# Default to Release mode unless overridden with -DCMAKE_BUILD_TYPE=Debug on cmake command
if(NOT CMAKE_BUILD_TYPE)
    message(STATUS "Forcing Release build type")
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
    set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

    # Set the possible values of build type for cmake-gui
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

# Paths
set(resources_directory ${CMAKE_CURRENT_SOURCE_DIR}/assets)
set(sources_directory ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(include_directory ${CMAKE_CURRENT_SOURCE_DIR}/include)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin)
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_BINARY_DIR}/bin)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake_modules/")

# Get git commit information using gitversion.cmake module
set(local_dir ${CMAKE_CURRENT_SOURCE_DIR})
include(gitversion)
# Include DEB package filename generation module
include(DebPackageFilename)

# Configure Boost
set(Boost_USE_STATIC_LIBS OFF)
set(Boost_USE_MULTITHREADED ON)
set(Boost_USE_STATIC_RUNTIME OFF)

add_definitions(-DBOOST_ALL_DYN_LINK)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    message(STATUS "Disabling Boost DEBUG logs")
    add_definitions(-DNDEBUG)
endif()

if (WIN32)
    set(WINSOCK2_LIBRARIES "ws2_32")
endif (WIN32)



# Building on a Mac requires Abseil
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")  # macOS
    message(STATUS "MacOS System Detected")
    add_definitions(-DMAC_OS)
    find_package(protobuf REQUIRED CONFIG)
    find_package(absl REQUIRED)
    set(CMAKE_PREFIX_PATH "/usr/local/opt/qt@5" ${CMAKE_PREFIX_PATH})
    list(APPEND CMAKE_PREFIX_PATH /usr/local/opt/util-linux)
    include(FindProtobuf)
else ()
    # Linux specific command
    find_path(Protobuf_INCLUDE_DIR google/protobuf/message.h PATHS /usr/include /usr/local/include)
    find_library(Protobuf_LIBRARY protobuf PATHS /usr/lib /usr/lib/x86_64-linux-gnu /usr/lib/aarch64-linux-gnu /usr/lib/arm-linux-gnueabihf /usr/local/lib)
    find_program(PROTOBUF_PROTOC_EXECUTABLE protoc)
    message(STATUS "Found Protobuf include: ${Protobuf_INCLUDE_DIR}")
    message(STATUS "Found Protobuf library: ${Protobuf_LIBRARY}")
    message(STATUS "Found protoc: ${PROTOBUF_PROTOC_EXECUTABLE}")
    if(NOT Protobuf_INCLUDE_DIR OR NOT Protobuf_LIBRARY OR NOT PROTOBUF_PROTOC_EXECUTABLE)
        message(FATAL_ERROR "Protobuf not found. Please install libprotobuf-dev and protobuf-compiler")
    endif()
    set(Protobuf_FOUND TRUE)
    set(PROTOBUF_LIBRARIES ${Protobuf_LIBRARY})
    set(PROTOBUF_INCLUDE_DIR ${Protobuf_INCLUDE_DIR})
endif ()

# aasdk from the OpenHU fork (https://github.com/average-web-dev/aasdk, branch openhu).
# Required version: any release with the same major version and at least this minor version.
# find_package checks it here, the Depends of the .deb are derived from it below.
set(OPENHU_AASDK_VERSION 1.0)
find_package(aasdk ${OPENHU_AASDK_VERSION} CONFIG REQUIRED)
message(STATUS "aasdk ${aasdk_VERSION}: ${aasdk_DIR}")

find_package(Boost REQUIRED COMPONENTS system log_setup log OPTIONAL_COMPONENTS unit_test_framework)
find_package(libusb-1.0 REQUIRED)
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui DBus Multimedia Network Qml Quick WebSockets)
# H.264 decoder of the Android Auto video (Projection/FfmpegVideoOutput)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavcodec libavutil libswscale)
find_package(OpenSSL REQUIRED)
find_package(rtaudio REQUIRED)
find_package(gps REQUIRED)

include_directories(${CMAKE_CURRENT_BINARY_DIR}
        ${Boost_INCLUDE_DIRS}
        ${OPENSSL_INCLUDE_DIR}
        ${RTAUDIO_INCLUDE_DIRS}
        ${include_directory}
        ${PROTOBUF_INCLUDE_DIR})

link_directories(${CMAKE_LIBRARY_OUTPUT_DIRECTORY})

set(common_include_directory ${include_directory}/f1x/openauto/Common)

set(autoapp_sources_directory ${sources_directory}/autoapp)
set(autoapp_include_directory ${include_directory}/f1x/openauto/autoapp)

file(GLOB_RECURSE autoapp_source_files ${autoapp_sources_directory}/*.cpp ${autoapp_include_directory}/*.hpp ${common_include_directory}/*.hpp ${resources_directory}/*.qrc ${autoapp_sources_directory}/*.qrc)

# Everything except main() and the QML resources is a library, so the tests link the same code.
set(autoapp_main_file ${autoapp_sources_directory}/autoapp.cpp)
set(autoapp_resource_files ${autoapp_source_files})
list(FILTER autoapp_resource_files INCLUDE REGEX "\\.qrc$")
list(FILTER autoapp_source_files EXCLUDE REGEX "\\.qrc$")
list(REMOVE_ITEM autoapp_source_files ${autoapp_main_file})

add_library(openhu_core STATIC ${autoapp_source_files})

target_link_libraries(openhu_core PUBLIC
        libusb
        ${Boost_LIBRARIES}
        Qt6::Core
        Qt6::Gui
        Qt6::DBus
        Qt6::Multimedia
        Qt6::Network
        Qt6::Qml
        Qt6::Quick
        Qt6::WebSockets
        PkgConfig::FFMPEG
        ${WINSOCK2_LIBRARIES}
        ${RTAUDIO_LIBRARIES}
        ${GPS_LIBRARIES}
        ${PROTOBUF_LIBRARIES}
        AASDK::aasdk)

add_executable(autoapp ${autoapp_main_file} ${autoapp_resource_files})
target_link_libraries(autoapp PRIVATE openhu_core)

option(OPENHU_TESTS "Build the unit tests (ctest)" ON)
if(OPENHU_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

set(PROGRAM_VERSION_STRING "${OPENAUTO_BUILD_MAJOR_RELEASE}.${OPENAUTO_BUILD_MINOR_RELEASE}.${OPENAUTO_BUILD_PATCH_RELEASE}")

# Add git commit info to version if available
if(GIT_COMMIT_ID AND NOT GIT_COMMIT_ID STREQUAL "unknown")
    set(PROGRAM_VERSION_STRING "${PROGRAM_VERSION_STRING}+git.${GIT_COMMIT_ID}")
    if(GIT_DIRTY GREATER 0)
        set(PROGRAM_VERSION_STRING "${PROGRAM_VERSION_STRING}.dirty")
    endif()
endif()

# Add build type suffix for debug builds
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(PROGRAM_VERSION_STRING "${PROGRAM_VERSION_STRING}.debug")
endif()

message(STATUS "Project Version: ${PROGRAM_VERSION_STRING}")
target_compile_definitions(openhu_core PRIVATE OPENHU_VERSION="${PROGRAM_VERSION_STRING}" OPENHU_GIT="${GIT_DESCRIBE}")
message(STATUS "Git Branch: ${GIT_BRANCH}")
message(STATUS "Git Commit: ${GIT_COMMIT_ID}")
message(STATUS "Git Describe: ${GIT_DESCRIBE}")
message(STATUS "Git Timestamp: ${GIT_COMMIT_TIMESTAMP}")
if(GIT_DIRTY GREATER 0)
    message(STATUS "Repository Status: DIRTY (has uncommitted changes)")
else()
    message(STATUS "Repository Status: CLEAN")
endif()

set_target_properties(autoapp
    PROPERTIES VERSION ${PROGRAM_VERSION_STRING} SOVERSION ${OPENAUTO_BUILD_MAJOR_RELEASE})

# Install rules
install(TARGETS autoapp
    RUNTIME DESTINATION bin
)

# Install udev rules
install(FILES packaging/udev/99-openhu.rules
    DESTINATION /lib/udev/rules.d
)

# Install systemd service files
install(FILES 
    packaging/systemd/openhu.service
    DESTINATION /lib/systemd/system
)

# Packaging configuration (DEB via CPack)
# Auto-detect architecture for proper Debian packaging
execute_process(
    COMMAND dpkg --print-architecture
    OUTPUT_VARIABLE DETECTED_ARCH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(DETECTED_ARCH)
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "${DETECTED_ARCH}")
    message(STATUS "Detected architecture for packaging: ${DETECTED_ARCH}")
else()
    # Fallback to system processor
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
        set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "arm64")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "armv7l")
        set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "armhf")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "i386|i686")
        set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "i386")
    else()
        set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "unknown")
    endif()
    message(STATUS "Fallback architecture detection: ${CPACK_DEBIAN_PACKAGE_ARCHITECTURE}")
endif()

set(CPACK_GENERATOR "DEB")

# Add debug suffix to package name for debug builds
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CPACK_PACKAGE_NAME "openhu-dbg")
else()
    set(CPACK_PACKAGE_NAME "openhu")
endif()

set(CPACK_PACKAGE_VENDOR "OpenCarDev")
set(CPACK_PACKAGE_CONTACT "OpenCarDev Team <opencardevuk@gmail.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "OpenHU head unit: Android Auto, plugins and API (based on OpenAuto)")
set(CPACK_PACKAGE_DESCRIPTION "OpenHU is a head unit for Raspberry Pi and other Linux devices: Android Auto inside a QML shell, a WebSocket API and plugins (CAN bus, steering wheel keys, camera, radio). Based on OpenAuto by f1x.studio and OpenCarDev.")
set(CPACK_PACKAGE_VERSION "${PROGRAM_VERSION_STRING}")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/average-web-dev/openhu")

# Debian package configuration
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "OpenCarDev Team <opencardevuk@gmail.com>")
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/average-web-dev/openhu")
# same binaries as the openauto package
set(CPACK_DEBIAN_PACKAGE_CONFLICTS "openauto")
set(CPACK_DEBIAN_PACKAGE_REPLACES "openauto")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

# Libraries come from shlibdeps (Qt 6, FFmpeg for the video); the QML modules of the shell, the SVG image plugin
# (plugin icons), pactl for the volume
# Wireless Android Auto and car radio: BlueZ for Bluetooth, NetworkManager for the hotspot, iw for the WiFi country
# Plugins written in Python use the API client of openhu-plugins (python3-websockets)
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "bluez, network-manager, iw, python3, python3-websockets")
string(REGEX MATCH "^[0-9]+" _aasdk_major "${OPENHU_AASDK_VERSION}")
math(EXPR _aasdk_next_major "${_aasdk_major} + 1")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libaasdk (>= ${OPENHU_AASDK_VERSION}), libaasdk (<< ${_aasdk_next_major}), pulseaudio-utils, qt6-svg-plugins, qml6-module-qtquick, qml6-module-qtquick-window, qml6-module-qtquick-layouts, qml6-module-qtquick-controls, qml6-module-qtmultimedia")

# Debian package control scripts
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/debian/postinst"
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/debian/prerm"
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/debian/postrm"
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/debian/preinst"
)

# Configure DEB package filename using the DebPackageFilename module
configure_deb_filename(
    PACKAGE_NAME "${CPACK_PACKAGE_NAME}"
    VERSION "${PROGRAM_VERSION_STRING}"
    ARCHITECTURE "${CPACK_DEBIAN_PACKAGE_ARCHITECTURE}"
)

# Optional: add basic metadata
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")

# Source package configuration
set(CPACK_SOURCE_GENERATOR "TGZ;TBZ2")
set(CPACK_SOURCE_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${PROGRAM_VERSION_STRING}-src")
set(CPACK_SOURCE_IGNORE_FILES
    "/build/"
    "/build-debug/"
    "/[.]git/"
    "/[.]vscode/"
    "/[.]idea/"
    "[.]gitignore"
    "[.]DS_Store"
    "/lib/"
    "/bin/"
    "[.]so$"
    "[.]a$"
)

include(CPack)