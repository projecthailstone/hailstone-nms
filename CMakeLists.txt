# The NMS server's plugin for Hailstone (docs/nms.md): C++ built against the client's plugin SDK (sdk/) alone, with
# its Lua windows for each UI pack. Built two ways:
#
# Inside the client's tree (clone this repository into the client as plugins/nms): the client's CMakeLists.txt adds
# it (add_subdirectory) when the folder is there, and gets
#   nms                the plugin, in build/hailstone/plugins/nms/ with its ui/
#   NMSC_NMS_LOGIC     the plugin's logic, also built into nmsc_tests and the libFuzzer target
#   NMSC_NMS_TESTS     the plugin's tests, added to nmsc_tests
#   NMSC_NMS_DIR       this folder (include path for the tests; tests/ holds fuzz_decoders_nms.inc)
#
# On its own, for the add-on's packages (tools/release.sh):
#   cmake -S . -B build -DHAILSTONE_CLIENT_DIR=../hailstone-client && cmake --build build
# gives build/nms/: the library and ui/, the folder tools/make_addon.py packs for one platform. No tests then.

cmake_minimum_required(VERSION 3.24)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(hailstone_nms LANGUAGES CXX)
  set(HAILSTONE_CLIENT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../hailstone-client" CACHE PATH
      "A checkout of hailstone-client: its sdk/ is the only part the plugin is built against")
  if(NOT EXISTS ${HAILSTONE_CLIENT_DIR}/sdk/nmsc_plugin.h)
    message(FATAL_ERROR "No sdk/nmsc_plugin.h under HAILSTONE_CLIENT_DIR (${HAILSTONE_CLIENT_DIR})")
  endif()
  set(NMSC_SDK_DIR ${HAILSTONE_CLIENT_DIR}/sdk)
  set(NMSC_TREE_DIR ${CMAKE_BINARY_DIR}) # the plugin lands in build/nms/
  if(MSVC)
    set(NMSC_WARNINGS /W4 /permissive- /utf-8)
  else()
    set(NMSC_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion
        -Wno-missing-field-initializers)
  endif()
  set(NMSC_NMS_STANDALONE ON)
else()
  set(NMSC_SDK_DIR ${PROJECT_SOURCE_DIR}/sdk)
  set(NMSC_NMS_STANDALONE OFF)
endif()

set(NMSC_NMS_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(NMSC_NMS_LOGIC)
foreach(f nms_rules nms_packets nms_loot nms_vault nms_waypoints nms_ini nms_options nms_petcmd)
  list(APPEND NMSC_NMS_LOGIC ${NMSC_NMS_DIR}/${f}.cpp)
endforeach()
set(NMSC_NMS_TESTS)
foreach(f nms_rules_test nms_plugin_test nms_scribe_test)
  list(APPEND NMSC_NMS_TESTS ${NMSC_NMS_DIR}/tests/${f}.cpp)
endforeach()

add_library(nms MODULE nms_plugin.cpp nms_labels.cpp nms_ui.cpp ${NMSC_NMS_LOGIC})
target_include_directories(nms PRIVATE ${NMSC_SDK_DIR})
target_compile_features(nms PRIVATE cxx_std_20)
target_compile_options(nms PRIVATE ${NMSC_WARNINGS})
set_target_properties(nms PROPERTIES PREFIX "" CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON
                                     LIBRARY_OUTPUT_DIRECTORY ${NMSC_TREE_DIR}/plugins/nms$<0:>
                                     RUNTIME_OUTPUT_DIRECTORY ${NMSC_TREE_DIR}/plugins/nms$<0:>)
if(NMSC_NMS_STANDALONE)
  set_target_properties(nms PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${NMSC_TREE_DIR}/nms$<0:>
                                       RUNTIME_OUTPUT_DIRECTORY ${NMSC_TREE_DIR}/nms$<0:>)
endif()
if(MSVC)
  target_compile_definitions(nms PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
add_custom_command(TARGET nms POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E rm -rf $<TARGET_FILE_DIR:nms>/ui
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${NMSC_NMS_DIR}/ui $<TARGET_FILE_DIR:nms>/ui)

# cmake --install build --prefix <dir> --component nms
install(TARGETS nms LIBRARY DESTINATION plugins/nms COMPONENT nms EXCLUDE_FROM_ALL) # a MODULE is a LIBRARY
install(DIRECTORY ${NMSC_NMS_DIR}/ui/ DESTINATION plugins/nms/ui COMPONENT nms EXCLUDE_FROM_ALL)

if(NOT NMSC_NMS_STANDALONE)
  set(NMSC_NMS_DIR ${NMSC_NMS_DIR} PARENT_SCOPE)
  set(NMSC_NMS_LOGIC ${NMSC_NMS_LOGIC} PARENT_SCOPE)
  set(NMSC_NMS_TESTS ${NMSC_NMS_TESTS} PARENT_SCOPE)
endif()
