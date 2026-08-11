cmake_minimum_required(VERSION 3.11)
project(ftnpkt
    VERSION 0.1
    DESCRIPTION "Utility for working with FidoNet packet (FTS-0001/FSC-0039/FSC-0048) files"
    LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

include(GNUInstallDirs)

add_compile_options(-Wall -Wextra -Wpedantic -Wshadow)

# iconv(): part of glibc (no extra lib on Fedora/RHEL), shipped as a separate
# libiconv on macOS/musl. The built-in FindIconv module covers both cases.
find_package(Iconv REQUIRED)

add_executable(ftnpkt
    src/main.c
    src/util.c
    src/addr.c
    src/charset.c
    src/packet.c
    src/message.c
    src/args.c
    src/cmd_create.c
    src/cmd_addmsg.c
    src/cmd_dump.c
)

target_include_directories(ftnpkt PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(ftnpkt PRIVATE Iconv::Iconv)

# Request POSIX.1-2008 declarations (clock_gettime, localtime_r, strdup,
# strndup, ...). Required under -std=c11 (CMAKE_C_EXTENSIONS OFF), where glibc
# otherwise hides them. Harmless on macOS/BSD.
target_compile_definitions(ftnpkt PRIVATE _POSIX_C_SOURCE=200809L)

# Version symbol for --version output.
target_compile_definitions(ftnpkt PRIVATE FTNPKT_VERSION="${PROJECT_VERSION}")

install(TARGETS ftnpkt RUNTIME DESTINATION bin)
install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR} RENAME LICENSE OPTIONAL)
