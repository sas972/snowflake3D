cmake_minimum_required(VERSION 3.22)
project(Snowflake3D LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

include(FetchContent)

# ------------------------------------------------------------------------------
# 1. Fetch Dear ImGui (pinned to master for latest imgui_impl_wgpu + Dawn support)
# ------------------------------------------------------------------------------
FetchContent_Declare(
    imgui
    URL https://github.com/ocornut/imgui/archive/refs/heads/master.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(imgui)

set(IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_wgpu.cpp
)

set(APP_SOURCES
    src/main.cpp
    src/Reiter3D.cpp
    src/Renderer3D.cpp
)

# ------------------------------------------------------------------------------
# 2. Emscripten (WebAssembly + emdawnwebgpu + SDL3)
# ------------------------------------------------------------------------------
if(EMSCRIPTEN)
    message(STATUS "[Snowflake3D] Configuring Emscripten WebAssembly build (Dawn / emdawnwebgpu + SDL3)...")

    set(EMDAWN_VERSION "v20261111.144539" CACHE STRING "Google Dawn release tag for emdawnwebgpu")
    FetchContent_Declare(
        emdawnwebgpu
        URL "https://github.com/google/dawn/releases/download/${EMDAWN_VERSION}/emdawnwebgpu_pkg-${EMDAWN_VERSION}.zip"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(emdawnwebgpu)
    set(EMDAWN_PORT_FLAG "--use-port=${emdawnwebgpu_SOURCE_DIR}/emdawnwebgpu.port.py")

    add_executable(snowflake3d ${APP_SOURCES} ${IMGUI_SOURCES})

    target_include_directories(snowflake3d PRIVATE
        src
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
    )

    target_compile_definitions(snowflake3d PUBLIC
        IMGUI_IMPL_WEBGPU_BACKEND_DAWN
        IMGUI_DISABLE_FILE_FUNCTIONS
    )

    target_compile_options(snowflake3d PUBLIC
        -O3
        "${EMDAWN_PORT_FLAG}"
        "-sUSE_SDL=3"
        "-sDISABLE_EXCEPTION_CATCHING=1"
    )

    target_link_options(snowflake3d PRIVATE
        -O3
        "${EMDAWN_PORT_FLAG}"
        "--closure-args=--externs=${emdawnwebgpu_SOURCE_DIR}/webgpu/src/webgpu-externs.js"
        "-sUSE_SDL=3"
        "-sWASM=1"
        "-sASYNCIFY=1"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sINITIAL_MEMORY=134217728"
        "-sSTACK_SIZE=2MB"
        "-sNO_EXIT_RUNTIME=0"
        "-sDISABLE_EXCEPTION_CATCHING=1"
        "--shell-file=${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html"
    )

    set_target_properties(snowflake3d PROPERTIES
        OUTPUT_NAME "index"
        SUFFIX ".html"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/web"
    )

# ------------------------------------------------------------------------------
# 3. Native Desktop Build (Google Dawn + SDL3 + Dear ImGui)
# ------------------------------------------------------------------------------
else()
    message(STATUS "[Snowflake3D] Configuring Native Desktop build (Google Dawn + SDL3)...")

    find_package(Threads REQUIRED)
    find_package(SDL3 REQUIRED)

    # Check if local cached Dawn prebuilt exists in .cache/dawn-prebuilt
    set(LOCAL_DAWN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.cache/dawn-prebuilt")
    if(EXISTS "${LOCAL_DAWN_DIR}/lib/cmake/Dawn/DawnConfig.cmake")
        message(STATUS "[Snowflake3D] Using cached Google Dawn prebuilt from ${LOCAL_DAWN_DIR}")
        list(APPEND CMAKE_PREFIX_PATH "${LOCAL_DAWN_DIR}")
    else()
        set(DAWN_RELEASE_TAG "v20261111.144539")
        set(DAWN_COMMIT_HASH "e327e2a60b08179ee8a04056f58dad8ca4b69b5f")
        if(APPLE)
            if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
                set(DAWN_PLATFORM "macos-latest")
            else()
                set(DAWN_PLATFORM "macos-15-intel")
            endif()
        elseif(UNIX)
            set(DAWN_PLATFORM "ubuntu-latest")
        elseif(WIN32)
            set(DAWN_PLATFORM "windows-latest")
        endif()
        set(DAWN_URL "https://github.com/google/dawn/releases/download/${DAWN_RELEASE_TAG}/Dawn-${DAWN_COMMIT_HASH}-${DAWN_PLATFORM}-Release.tar.gz")
        message(STATUS "[Snowflake3D] Fetching official Google Dawn prebuilt from ${DAWN_URL}...")
        FetchContent_Declare(
            dawn_prebuilt
            URL "${DAWN_URL}"
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
        FetchContent_MakeAvailable(dawn_prebuilt)
        list(APPEND CMAKE_PREFIX_PATH "${dawn_prebuilt_SOURCE_DIR}")
    endif()

    find_package(Dawn CONFIG REQUIRED)

    if(APPLE)
        set_source_files_properties(${imgui_SOURCE_DIR}/backends/imgui_impl_wgpu.cpp PROPERTIES COMPILE_FLAGS "-x objective-c++")
        set(OS_LIBRARIES "-framework CoreFoundation -framework QuartzCore -framework Metal -framework MetalKit -framework Cocoa -framework IOSurface -framework IOKit -framework Foundation")
    endif()

    add_executable(snowflake3d ${APP_SOURCES} ${IMGUI_SOURCES})

    target_include_directories(snowflake3d PRIVATE
        src
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
    )

    target_compile_definitions(snowflake3d PUBLIC
        IMGUI_IMPL_WEBGPU_BACKEND_DAWN
    )

    target_compile_options(snowflake3d PRIVATE
        -O3
        -Wall
        -Wextra
    )

    target_link_libraries(snowflake3d PRIVATE
        dawn::webgpu_dawn
        dawn::webgpu_upstream_cpp_headers
        SDL3::SDL3
        Threads::Threads
        ${OS_LIBRARIES}
    )
endif()

