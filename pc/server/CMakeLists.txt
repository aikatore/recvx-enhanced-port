# recvx-server: co-op rendezvous + relay (docs/SERVER.md). No game, no SDL, no disc.
# Part of the game's build (pc/CMakeLists.txt adds it), or on its own for a server machine:
#   cmake -S pc/server -B build-server -DCMAKE_BUILD_TYPE=Release && cmake --build build-server
cmake_minimum_required(VERSION 3.20)
if(NOT DEFINED PROJECT_NAME)
    # standalone: the release version of the top-level CMakeLists.txt
    file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/../../CMakeLists.txt _v REGEX "^project\\(recvx_pc VERSION")
    string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" _v "${_v}")
    project(recvx_server VERSION ${_v} LANGUAGES C)
    enable_testing()
    include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/version.cmake)
endif()

# Monocypher (BSD-2-Clause / CC0): BLAKE2b, Argon2id, X25519, XChaCha20-Poly1305. Shared with the game.
if(NOT TARGET monocypher)
    include(FetchContent)
    FetchContent_Declare(monocypher GIT_REPOSITORY https://github.com/LoupVaillant/Monocypher.git GIT_TAG 4.0.2 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(monocypher)   # no CMakeLists.txt there: only fetched
    add_library(monocypher STATIC ${monocypher_SOURCE_DIR}/src/monocypher.c)
    target_include_directories(monocypher PUBLIC ${monocypher_SOURCE_DIR}/src)
    target_compile_options(monocypher PRIVATE -O2)
endif()

set(RDV_PROTO ${CMAKE_CURRENT_SOURCE_DIR}/../src/net/rdv_proto.c)
add_library(recvx_server_core STATIC server.c config.c ${RDV_PROTO})
target_compile_options(recvx_server_core PRIVATE -std=gnu11 -Wall)
target_link_libraries(recvx_server_core PUBLIC monocypher)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    option(RECVX_SERVER_STATIC "Link recvx-server statically" ON)
    add_executable(recvx-server main.c)
    set_property(SOURCE main.c APPEND PROPERTY COMPILE_DEFINITIONS "RECVX_VERSION=\"${RECVX_VERSION}\"" "RECVX_RELEASE=\"${PROJECT_VERSION}\"")
    target_compile_options(recvx-server PRIVATE -std=gnu11 -Wall)
    target_link_libraries(recvx-server PRIVATE recvx_server_core)
    if(RECVX_SERVER_STATIC)
        target_link_options(recvx-server PRIVATE -static)
    endif()

    # recvx-client: the game's client code (rendezvous.c, net.c without SDL) with a fake game
    # stream, for testing a server and measuring the paths without the game
    add_executable(recvx-client client.c ../src/net/rendezvous.c ../src/net/net.c)
    target_compile_definitions(recvx-client PRIVATE RECVX_NET_NO_SDL)
    target_compile_options(recvx-client PRIVATE -std=gnu11 -Wall)
    target_link_libraries(recvx-client PRIVATE recvx_server_core)

    # ctest rdv_paths: a local server and two recvx-clients: punched, relay forced, relay fallback,
    # passwords, loss, an unknown room, unencrypted (~45 s)
    add_test(NAME rdv_paths COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test_paths.sh $<TARGET_FILE_DIR:recvx-client>)
    # ctest rdv_nat: cone / symmetric NAT, same router, netem delay and loss, in rootless network
    # namespaces (unshare -rn, nftables, tc); skipped where user namespaces are not allowed (~45 s)
    add_test(NAME rdv_nat COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test_nat.sh $<TARGET_FILE_DIR:recvx-client>)
    set_tests_properties(rdv_nat PROPERTIES SKIP_RETURN_CODE 77)
    # ctest rdv_handover: --take-over (refused by an incompatible build, then done by a second
    # binary while relay traffic flows), status-json, --drain, the control socket, slow and queued
    # take-overs leave exactly one server (~35 s)
    add_test(NAME rdv_handover COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test_handover.sh $<TARGET_FILE_DIR:recvx-client>)
    set_tests_properties(rdv_handover PROPERTIES SKIP_RETURN_CODE 77)
endif()
