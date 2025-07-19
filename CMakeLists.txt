cmake_minimum_required(VERSION 3.16)
project(LeftoverPasta VERSION 0.0.1 LANGUAGES CXX)

# C++ Standard

set(CMAKE_CXX_STANDARD 20)

if(APPLE)
    set(PLATFORM_DIR "macos")
elseif(WIN32)
    set(PLATFORM_DIR "windows")
endif ()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/build/bin/${PLATFORM_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/build/bin/${PLATFORM_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/build/bin/${PLATFORM_DIR})

# SDL3
set(SDL3_DIR "${CMAKE_SOURCE_DIR}/build/sdl3-build")
set(CMAKE_PREFIX_PATH "${SDL3_DIR}; ${CMAKE_PREFIX_PATH}")
find_package(SDL3 REQUIRED)


configure_file(
        ${CMAKE_SOURCE_DIR}/Info.plist.in
        ${CMAKE_BINARY_DIR}/Info.plist
        @ONLY
)
# executable
add_executable(LeftoverPasta
        src/path_fiddle.cpp
        src/fiddle_context.hpp
        src/fiddle_context_metal.mm
        src/fiddle_context_vulkan.cpp
        src/asset_utils.cpp
)

#copy assets into the bin
if (APPLE)
    set(ASSET_DEST "$<TARGET_FILE_DIR:LeftoverPasta>/../Resources")
else ()
    set(ASSET_DEST "$<TARGET_FILE_DIR:LeftoverPasta>/assets")
endif ()

add_custom_command(TARGET LeftoverPasta POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${ASSET_DEST}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets" "${ASSET_DEST}"
        COMMENT "Copying assets to output directory"
)


# SDL3 specific callback stuff
target_compile_definitions(LeftoverPasta PRIVATE
        SDL_MAIN_USE_CALLBACKS
)

# Include directories
target_include_directories(LeftoverPasta PRIVATE
        src
        dependencies/rive-runtime/include
        dependencies/rive-runtime/renderer/include
        dependencies/rive-runtime/renderer/glad

)

# Target link directories for Rive
target_link_directories(LeftoverPasta PRIVATE
        ${CMAKE_SOURCE_DIR}/build/rive-build

)

#Link Libraries
target_link_libraries(LeftoverPasta PRIVATE
        SDL3::SDL3
        rive
        rive_pls_renderer
        rive_decoders
        rive_harfbuzz
        rive_sheenbidi
        rive_yoga
        libwebp
        libpng
        zlib
)

#macos bundle

set_target_properties(LeftoverPasta PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_BINARY_DIR}/Info.plist"
        MACOSX_BUNDLE_BUNDLE_NAME "Leftover Pasta"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.evilbean.leftoverpasta"
)

# Platform-specific

if(APPLE)
    target_link_libraries(LeftoverPasta PRIVATE
            Cocoa
            Metal
            QuartzCore
            IOKit
            "-framework OpenGL"
    )
    target_compile_options(LeftoverPasta PRIVATE -fobjc-arc)
elseif (WIN32)
    target_link_libraries(LeftoverPasta PRIVATE
            opengl32
            d3d11
            d3d12
            dxguid
            dxgi
    )
endif()