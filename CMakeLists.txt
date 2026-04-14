cmake_minimum_required(VERSION 3.25)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
project(skill-editor VERSION 0.1.0 LANGUAGES C CXX DESCRIPTION "LEGO Universe behavior tree skill editor")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

include(FetchContent)
FetchContent_Declare(tool_common
    GIT_REPOSITORY https://github.com/LU-Rebuilt/tool-common.git
    GIT_TAG main
)
FetchContent_MakeAvailable(tool_common)

FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
# SQLite3 — try system, fall back to FetchContent
find_package(SQLite3 QUIET)
if(NOT SQLite3_FOUND)
    FetchContent_Declare(sqlite3
        URL https://www.sqlite.org/2024/sqlite-amalgamation-3460100.zip
    )
    FetchContent_MakeAvailable(sqlite3)
    add_library(sqlite3_lib STATIC ${sqlite3_SOURCE_DIR}/sqlite3.c)
    target_include_directories(sqlite3_lib PUBLIC ${sqlite3_SOURCE_DIR})
    if(NOT TARGET SQLite::SQLite3)
        add_library(SQLite::SQLite3 ALIAS sqlite3_lib)
    endif()
endif()

add_executable(skill_editor
    src/main.cpp
    src/behavior_model.cpp src/behavior_model.h
    src/tree_scene.cpp src/tree_scene.h
    src/node_editor.cpp src/node_editor.h
    src/main_window.cpp src/main_window.h
    src/zoomable_view.h
    src/param_delegate.cpp src/param_delegate.h
    src/db_source.cpp src/db_source.h
    src/undo_commands.cpp src/undo_commands.h
    src/object_skills_panel.cpp src/object_skills_panel.h
)
target_link_libraries(skill_editor PRIVATE
    Qt6::Widgets SQLite::SQLite3
    qt_common nlohmann_json::nlohmann_json
)
target_include_directories(skill_editor PRIVATE src)

# Copy behavior_schema.json next to the executable
add_custom_command(TARGET skill_editor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/src/behavior_schema.json
        $<TARGET_FILE_DIR:skill_editor>/behavior_schema.json
)

# Embed behavior_schema.json as a C byte array for fallback
# (MSVC has a 16KB raw string literal limit, so we use a byte array instead)
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/src/behavior_schema.json SCHEMA_HEX HEX)
string(LENGTH "${SCHEMA_HEX}" SCHEMA_HEX_LEN)
math(EXPR SCHEMA_BYTE_LEN "${SCHEMA_HEX_LEN} / 2")
# Convert hex pairs to 0xNN, comma-separated
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," SCHEMA_BYTES "${SCHEMA_HEX}")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/embedded_schema.h
    "#pragma once\n"
    "#include <cstddef>\n"
    "// Auto-generated — do not edit. Embedded from behavior_schema.json.\n"
    "static const unsigned char EMBEDDED_BEHAVIOR_SCHEMA_DATA[] = {\n"
    "${SCHEMA_BYTES}\n"
    "};\n"
    "inline const char* const EMBEDDED_BEHAVIOR_SCHEMA = reinterpret_cast<const char*>(EMBEDDED_BEHAVIOR_SCHEMA_DATA);\n"
    "inline const size_t EMBEDDED_BEHAVIOR_SCHEMA_LEN = ${SCHEMA_BYTE_LEN};\n"
)
target_include_directories(skill_editor PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
