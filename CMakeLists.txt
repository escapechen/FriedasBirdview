cmake_minimum_required(VERSION 3.21)

project(FriedasBirdview VERSION 1.9.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(FRIEDASBIRDVIEW_PLATFORM_SOURCES)
set(FRIEDASBIRDVIEW_QT_COMPONENTS Core Gui Widgets Network Multimedia)
set(FRIEDASBIRDVIEW_APPLICATION_ID "org.friedasbirdview.FriedasBirdview" CACHE STRING
    "Freedesktop application ID used for installed desktop metadata")
option(FRIEDASBIRDVIEW_FLATPAK "Build for a Flatpak sandbox" OFF)
if(WIN32)
    set(FRIEDASBIRDVIEW_ABOUT_PLATFORM "Windows 11")
    list(APPEND FRIEDASBIRDVIEW_PLATFORM_SOURCES
        src/AutostartManagerWindows.cpp
        src/CredentialStoreWindows.cpp
        src/CustomCaPlatformWindows.cpp
        src/StreamViewWindows.cpp
    )

    set(FRIEDASBIRDVIEW_WEBVIEW2_SDK_ROOT "" CACHE PATH
        "Extracted Microsoft.Web.WebView2 SDK root")
    if(NOT FRIEDASBIRDVIEW_WEBVIEW2_SDK_ROOT AND DEFINED ENV{WEBVIEW2_SDK_ROOT})
        set(FRIEDASBIRDVIEW_WEBVIEW2_SDK_ROOT "$ENV{WEBVIEW2_SDK_ROOT}")
    endif()
    set(FRIEDASBIRDVIEW_WEBVIEW2_INCLUDE_DIR
        "${FRIEDASBIRDVIEW_WEBVIEW2_SDK_ROOT}/build/native/include")
    set(FRIEDASBIRDVIEW_WEBVIEW2_LOADER
        "${FRIEDASBIRDVIEW_WEBVIEW2_SDK_ROOT}/build/native/x64/WebView2LoaderStatic.lib")
    if(NOT EXISTS "${FRIEDASBIRDVIEW_WEBVIEW2_INCLUDE_DIR}/WebView2.h"
        OR NOT EXISTS "${FRIEDASBIRDVIEW_WEBVIEW2_LOADER}")
        message(FATAL_ERROR
            "The Microsoft WebView2 SDK is required on Windows. Run packaging/windows/ensure-webview2-sdk.ps1 or set FRIEDASBIRDVIEW_WEBVIEW2_SDK_ROOT.")
    endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(FRIEDASBIRDVIEW_ABOUT_PLATFORM "KDE Plasma on Linux")
    list(APPEND FRIEDASBIRDVIEW_QT_COMPONENTS
        MultimediaWidgets
        WebChannel
        WebEngineCore
        WebEngineWidgets
        WebSockets
        DBus
    )
    find_package(KF6Wallet 6.0 REQUIRED)
    list(APPEND FRIEDASBIRDVIEW_PLATFORM_SOURCES
        src/AutostartManager.cpp
        src/CredentialStoreLinux.cpp
        src/CustomCaPlatformLinux.cpp
        src/StreamView.cpp
    )
else()
    message(FATAL_ERROR "FriedasBirdview currently supports Linux and Windows only.")
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS ${FRIEDASBIRDVIEW_QT_COMPONENTS})
find_package(OpenSSL REQUIRED)

qt_standard_project_setup()

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
configure_file(
    src/AppBuildConfig.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/generated/AppBuildConfig.h"
    @ONLY
)
if(NOT WIN32)
    configure_file(
        resources/friedasbirdview.desktop.in
        "${CMAKE_CURRENT_BINARY_DIR}/generated/${FRIEDASBIRDVIEW_APPLICATION_ID}.desktop"
        @ONLY
    )
    configure_file(
        resources/friedasbirdview.metainfo.xml.in
        "${CMAKE_CURRENT_BINARY_DIR}/generated/${FRIEDASBIRDVIEW_APPLICATION_ID}.metainfo.xml"
        @ONLY
    )
endif()

qt_add_executable(friedasbirdview
    src/main.cpp
    src/AppController.cpp
    src/AppController.h
    src/AutostartManager.h
    src/CredentialStore.h
    src/CustomCaStore.cpp
    src/CustomCaStore.h
    src/CustomCaPlatform.h
    src/FrigateMonitor.cpp
    src/FrigateMonitor.h
    src/OverlayWindow.cpp
    src/OverlayWindow.h
    src/SettingsDialog.cpp
    src/SettingsDialog.h
    src/StreamView.h
    ${FRIEDASBIRDVIEW_PLATFORM_SOURCES}
)

qt_add_resources(friedasbirdview "friedasbirdview_resources"
    PREFIX "/"
    FILES
        docs/images/frieda-birdview-hero.png
        resources/icons/friedasbirdview.png
        resources/icons/friedasbirdview.svg
)

target_link_libraries(friedasbirdview PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Multimedia
    OpenSSL::Crypto
)

if(WIN32)
    target_include_directories(friedasbirdview PRIVATE "${FRIEDASBIRDVIEW_WEBVIEW2_INCLUDE_DIR}")
    target_link_libraries(friedasbirdview PRIVATE
        Advapi32
        Ole32
        Shlwapi
        "${FRIEDASBIRDVIEW_WEBVIEW2_LOADER}"
    )
else()
    target_link_libraries(friedasbirdview PRIVATE
        Qt6::MultimediaWidgets
        Qt6::WebChannel
        Qt6::WebEngineCore
        Qt6::WebEngineWidgets
        Qt6::WebSockets
        Qt6::DBus
        KF6::Wallet
    )
endif()

target_include_directories(friedasbirdview PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)

if(MSVC)
    # Keep the Windows executable opted into the compiler and loader mitigations
    # rather than relying on Visual Studio's changing defaults.
    target_compile_options(friedasbirdview PRIVATE /W4 /permissive- /sdl /guard:cf /utf-8)
    target_link_options(friedasbirdview PRIVATE /guard:cf /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA)
endif()

set_target_properties(friedasbirdview PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE FALSE
)

install(TARGETS friedasbirdview RUNTIME DESTINATION bin)
if(NOT WIN32)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/generated/${FRIEDASBIRDVIEW_APPLICATION_ID}.desktop"
        DESTINATION share/applications)
    install(FILES resources/icons/friedasbirdview.svg
        DESTINATION share/icons/hicolor/scalable/apps
        RENAME ${FRIEDASBIRDVIEW_APPLICATION_ID}.svg)
    install(FILES resources/icons/friedasbirdview.png
        DESTINATION share/icons/hicolor/512x512/apps
        RENAME ${FRIEDASBIRDVIEW_APPLICATION_ID}.png)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/generated/${FRIEDASBIRDVIEW_APPLICATION_ID}.metainfo.xml"
        DESTINATION share/metainfo)
endif()
