cmake_minimum_required(VERSION 3.21)

project(FriedasBirdview VERSION 1.5.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(FRIEDASBIRDVIEW_APPLICATION_ID "org.friedasbirdview.FriedasBirdview" CACHE STRING
    "Freedesktop application ID used for installed desktop metadata")
option(FRIEDASBIRDVIEW_FLATPAK "Build for a Flatpak sandbox" OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Network
    Multimedia
    WebChannel
    WebEngineCore
    WebEngineWidgets
)
find_package(OpenSSL REQUIRED)

set(FRIEDASBIRDVIEW_PLATFORM_SOURCES)
set(FRIEDASBIRDVIEW_WEBENGINE_PROFILE_CUSTOM_CA 0)
if(WIN32)
    set(FRIEDASBIRDVIEW_ABOUT_PLATFORM "Windows 11")
    if(Qt6_VERSION VERSION_LESS 6.10.0)
        message(FATAL_ERROR "The Windows port requires Qt 6.10 or newer for app-scoped WebEngine CA trust.")
    endif()
    list(APPEND FRIEDASBIRDVIEW_PLATFORM_SOURCES
        src/AutostartManagerWindows.cpp
        src/CredentialStoreWindows.cpp
        src/CustomCaPlatformWindows.cpp
    )
    set(FRIEDASBIRDVIEW_WEBENGINE_PROFILE_CUSTOM_CA 1)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(FRIEDASBIRDVIEW_ABOUT_PLATFORM "KDE Plasma on Linux")
    find_package(Qt6 6.5 REQUIRED COMPONENTS DBus)
    find_package(KF6Wallet 6.0 REQUIRED)
    list(APPEND FRIEDASBIRDVIEW_PLATFORM_SOURCES
        src/AutostartManager.cpp
        src/CredentialStoreLinux.cpp
        src/CustomCaPlatformLinux.cpp
    )
else()
    message(FATAL_ERROR "FriedasBirdview currently supports Linux and Windows only.")
endif()

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
    src/StreamView.cpp
    src/StreamView.h
    ${FRIEDASBIRDVIEW_PLATFORM_SOURCES}
)

qt_add_resources(friedasbirdview "friedasbirdview_resources"
    PREFIX "/"
    FILES
        resources/icons/friedasbirdview.png
        resources/icons/friedasbirdview.svg
)

target_link_libraries(friedasbirdview PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Multimedia
    Qt6::WebChannel
    Qt6::WebEngineCore
    Qt6::WebEngineWidgets
    OpenSSL::Crypto
)

if(WIN32)
    target_link_libraries(friedasbirdview PRIVATE Advapi32)
else()
    target_link_libraries(friedasbirdview PRIVATE Qt6::DBus KF6::Wallet)
endif()

target_include_directories(friedasbirdview PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)

target_compile_definitions(friedasbirdview PRIVATE
    FRIEDASBIRDVIEW_WEBENGINE_PROFILE_CUSTOM_CA=${FRIEDASBIRDVIEW_WEBENGINE_PROFILE_CUSTOM_CA}
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
