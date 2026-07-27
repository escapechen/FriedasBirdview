cmake_minimum_required(VERSION 3.21)

project(FriedasBirdview VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(FRIEDASBIRDVIEW_APPLICATION_ID "org.friedasbirdview.FriedasBirdview" CACHE STRING
    "Freedesktop application ID used for installed desktop metadata")
option(FRIEDASBIRDVIEW_FLATPAK "Build for a Flatpak sandbox" OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS
    Core
    DBus
    Gui
    Widgets
    Network
    Multimedia
    WebChannel
    WebEngineWidgets
)
find_package(KF6Wallet 6.0 REQUIRED)
find_package(OpenSSL REQUIRED)

qt_standard_project_setup()

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
configure_file(
    src/AppBuildConfig.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/generated/AppBuildConfig.h"
    @ONLY
)
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

qt_add_executable(friedasbirdview
    src/main.cpp
    src/AppController.cpp
    src/AppController.h
    src/AutostartManager.cpp
    src/AutostartManager.h
    src/CustomCaStore.cpp
    src/CustomCaStore.h
    src/FrigateMonitor.cpp
    src/FrigateMonitor.h
    src/OverlayWindow.cpp
    src/OverlayWindow.h
    src/SettingsDialog.cpp
    src/SettingsDialog.h
    src/StreamView.cpp
    src/StreamView.h
)

qt_add_resources(friedasbirdview "friedasbirdview_resources"
    PREFIX "/"
    FILES
        resources/icons/friedasbirdview.png
        resources/icons/friedasbirdview.svg
)

target_link_libraries(friedasbirdview PRIVATE
    Qt6::Core
    Qt6::DBus
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Multimedia
    Qt6::WebChannel
    Qt6::WebEngineWidgets
    KF6::Wallet
    OpenSSL::Crypto
)

target_include_directories(friedasbirdview PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)

set_target_properties(friedasbirdview PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE FALSE
)

install(TARGETS friedasbirdview RUNTIME DESTINATION bin)
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
