cmake_minimum_required(VERSION 3.20)

set(PLUGIN_NAME "sodium" CACHE STRING "Plugin name")

project(${PLUGIN_NAME} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_STANDARD 17)

# The libsodium-cmake target is already called "sodium"
set(PLUGIN_TARGET "${PLUGIN_NAME}_plugin")

set(SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../4D-Plugin-SDK/4D Plugin API")
set(LIBSODIUM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../libsodium-cmake")

# Static CRT (must be set before add_subdirectory so libsodium matches)
if(WIN32)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "" FORCE)
endif()

# libsodium (static, no tests)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SODIUM_DISABLE_TESTS ON CACHE BOOL "" FORCE)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
add_subdirectory("${LIBSODIUM_DIR}" "${CMAKE_BINARY_DIR}/libsodium-cmake" EXCLUDE_FROM_ALL)

set(PLUGIN_SOURCES
    ${PLUGIN_NAME}-4dplugin.cpp
    "${SDK_DIR}/4DPluginAPI.c"
)

set(PLUGIN_RESOURCES
    manifest.json
)

if(APPLE)
    add_library(${PLUGIN_TARGET} MODULE ${PLUGIN_SOURCES} ${PLUGIN_RESOURCES})

    set_target_properties(${PLUGIN_TARGET} PROPERTIES
        OUTPUT_NAME "${PLUGIN_NAME}"
        BUNDLE TRUE
        BUNDLE_EXTENSION "bundle"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.4d.${PLUGIN_NAME}"
        MACOSX_BUNDLE_BUNDLE_VERSION "1.0"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${PLUGIN_NAME}-test/Plugins"
        LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/${PLUGIN_NAME}-test/Plugins"
        LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/${PLUGIN_NAME}-test/Plugins"
    )

    set_source_files_properties(${PLUGIN_RESOURCES} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

    find_library(COREGRAPHICS_FRAMEWORK CoreGraphics REQUIRED)
    target_link_libraries(${PLUGIN_TARGET} PRIVATE ${COREGRAPHICS_FRAMEWORK})

    set_source_files_properties("${SDK_DIR}/4DPluginAPI.c" PROPERTIES
        COMPILE_FLAGS "-Wno-int-conversion -Wno-multichar")

elseif(WIN32)
    add_library(${PLUGIN_TARGET} SHARED ${PLUGIN_SOURCES})

    set_target_properties(${PLUGIN_TARGET} PROPERTIES
        OUTPUT_NAME "${PLUGIN_NAME}"
        SUFFIX ".4DX"
        LINK_FLAGS "/DEF:\"${SDK_DIR}/4DPluginAPI.def\""
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )

    set(PLUGIN_OUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/${PLUGIN_NAME}-test/Plugins/${PLUGIN_NAME}/Contents/Windows64")
    set(RESOURCE_OUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/${PLUGIN_NAME}-test/Plugins/${PLUGIN_NAME}/Contents/Resources")

    set_target_properties(${PLUGIN_TARGET} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PLUGIN_OUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${PLUGIN_OUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${PLUGIN_OUT_DIR}"
    )

    add_custom_command(TARGET ${PLUGIN_TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${RESOURCE_OUT_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/manifest.json" "${RESOURCE_OUT_DIR}"
        COMMENT "Copying manifest.json to Resources"
    )

    target_compile_options(${PLUGIN_TARGET} PRIVATE /wd4267 /wd4244 /wd4305)
endif()

set_source_files_properties("${SDK_DIR}/4DPluginAPI.c" PROPERTIES LANGUAGE CXX)

target_include_directories(${PLUGIN_TARGET} PRIVATE "${SDK_DIR}")
target_link_libraries(${PLUGIN_TARGET} PRIVATE sodium)
target_compile_definitions(${PLUGIN_TARGET} PRIVATE UNICODE _UNICODE SODIUM_STATIC)
