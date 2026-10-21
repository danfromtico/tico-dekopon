# tico-dekopon: Dekopon run from tico, with tico's overlay over Dekopon's renderer.
set(TICO_SHARED_DIR ${PROJECT_SOURCE_DIR}/tico)

add_executable(tico
    clocks.cpp
    clocks.h
    dekopon_features.cpp
    dekopon_features.h
    game_overlay.cpp
    game_overlay.h
    main.cpp
    overlay_renderer.cpp
    saves.cpp
    saves.h
    session_bridge.cpp
    session_bridge.h
    switch_libnx.h
    tico_settings.cpp
    tico_settings.h
    # tico's overlay, shared with the other cores
    ${TICO_SHARED_DIR}/UsbStorage.cpp
    ${TICO_SHARED_DIR}/UsbStorage.h
    ${TICO_SHARED_DIR}/overlay/imgui_overlay.cpp
    ${TICO_SHARED_DIR}/overlay/imgui_overlay.h
    ${TICO_SHARED_DIR}/overlay/overlay_renderer.h
    ${TICO_SHARED_DIR}/overlay/overlay_ui.cpp
    ${TICO_SHARED_DIR}/overlay/overlay_ui.h
    ${TICO_SHARED_DIR}/overlay/ra_alerts.cpp
    ${TICO_SHARED_DIR}/overlay/ra_alerts.h
    ${TICO_SHARED_DIR}/overlay/tico_config.cpp
    ${TICO_SHARED_DIR}/overlay/tico_config.h
    ${TICO_SHARED_DIR}/overlay/translation_manager.cpp
    ${TICO_SHARED_DIR}/overlay/translation_manager.h
)

create_target_directory_groups(tico)
target_include_directories(tico PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/.. ${TICO_SHARED_DIR})
target_link_libraries(tico PRIVATE
    dekopon_frontend
    dekopon_horizon_overrides
    imgui
    stb
    json-headers
)

# libsodium: TicoSecure opens the session tico seals for each launch
find_path(SWITCH_SODIUM_INCLUDE_DIR sodium.h REQUIRED)
find_library(SWITCH_SODIUM sodium REQUIRED)
# after Dekopon's own Vulkan headers: portlibs' include holds mesa-switch's newer ones too
target_include_directories(tico SYSTEM AFTER PRIVATE ${SWITCH_SODIUM_INCLUDE_DIR})
target_link_libraries(tico PRIVATE ${SWITCH_SODIUM})

# tico-dekopon's own release, apart from Dekopon's version
set(TICO_DEKOPON_VERSION "3.0.0")

nx_generate_nacp(OUTPUT tico-dekopon.nacp
    NAME "tico-dekopon"
    AUTHOR "Azahar Team \& PalindromicBreadLoaf \& ticoverse.com"
    VERSION "${TICO_DEKOPON_VERSION}"
)

# The overlay's fonts, strings and pictures, and the module's settings.json it lists, each
# copied into the romfs folder so a change to one packs the NRO again.
set(TICO_ROMFS_DIR "${CMAKE_CURRENT_BINARY_DIR}/romfs")
file(GLOB_RECURSE TICO_ROMFS_SOURCES CONFIGURE_DEPENDS
    "${TICO_SHARED_DIR}/fonts/*" "${TICO_SHARED_DIR}/lang/*" "${TICO_SHARED_DIR}/assets/*")
list(APPEND TICO_ROMFS_SOURCES "${TICO_SHARED_DIR}/module/settings.json")
set(TICO_ROMFS_FILES "")
foreach(source IN LISTS TICO_ROMFS_SOURCES)
    file(RELATIVE_PATH relative "${TICO_SHARED_DIR}" "${source}")
    set(destination "${TICO_ROMFS_DIR}/${relative}")
    add_custom_command(
        OUTPUT "${destination}"
        COMMAND ${CMAKE_COMMAND} -E copy "${source}" "${destination}"
        DEPENDS "${source}"
        VERBATIM
    )
    list(APPEND TICO_ROMFS_FILES "${destination}")
endforeach()
add_custom_target(tico_romfs_files DEPENDS ${TICO_ROMFS_FILES})
dkp_add_asset_target(tico_romfs "${TICO_ROMFS_DIR}")
add_dependencies(tico_romfs tico_romfs_files)
set_property(TARGET tico_romfs APPEND PROPERTY DKP_ASSET_FILES ${TICO_ROMFS_FILES})

nx_create_nro(tico
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/tico-dekopon.nro"
    NACP "${CMAKE_CURRENT_BINARY_DIR}/tico-dekopon.nacp"
    ICON "${PROJECT_SOURCE_DIR}/src/citra_switch/icon.jpg"
    ROMFS tico_romfs
)
