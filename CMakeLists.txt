cmake_minimum_required(VERSION 3.29)

project(OfflineCharacterOrder VERSION 0.9.35 LANGUAGES CXX RC)

include(FetchContent)

# Pin the public SDK to the exact ABI-4 revision used by this release.
set(D2RLPLUGIN_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(D2RLPLUGIN_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    D2RLoaderPluginSDK
    GIT_REPOSITORY https://github.com/D2RLoader/PluginSDK.git
    GIT_TAG 6eb8f8b6192868214706bd6d528c5294f2f551b7
    GIT_PROGRESS TRUE
)
FetchContent_MakeAvailable(D2RLoaderPluginSDK)

set(OFFLINE_CHARACTER_ORDER_LEFT_ARROW_SPRITE
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/native/sort-arrow-left.sprite")
set(OFFLINE_CHARACTER_ORDER_RIGHT_ARROW_SPRITE
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/native/sort-arrow-right.sprite")
set(OFFLINE_CHARACTER_ORDER_MODE_PLAQUE_SPRITE
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/native/sort-mode-plaque.sprite")

add_library(offline_character_order SHARED
    src/plugin.cpp
    src/plugin.rc
)
set_property(
    SOURCE src/plugin.rc
    APPEND PROPERTY OBJECT_DEPENDS
        "${OFFLINE_CHARACTER_ORDER_LEFT_ARROW_SPRITE};${OFFLINE_CHARACTER_ORDER_RIGHT_ARROW_SPRITE};${OFFLINE_CHARACTER_ORDER_MODE_PLAQUE_SPRITE}"
)
target_link_libraries(offline_character_order PRIVATE
    D2RLPlugin::D2RLPlugin
    User32
)
target_compile_features(offline_character_order PRIVATE cxx_std_20)
target_compile_definitions(offline_character_order PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE
)
target_include_directories(offline_character_order PRIVATE src)
set_target_properties(offline_character_order PROPERTIES
    OUTPUT_NAME "d2rl-offline-character-order"
    PREFIX ""
)

d2rlplugin_embed_config(
    TARGET offline_character_order
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/config/offline-character-order.toml"
)

if(MSVC)
    target_compile_options(offline_character_order PRIVATE
        /W4 /permissive- /EHsc /utf-8
    )
    target_link_options(offline_character_order PRIVATE /INCREMENTAL:NO /Brepro)
endif()
