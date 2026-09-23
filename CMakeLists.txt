cmake_minimum_required(VERSION 3.22)
project(cougar-rgb VERSION 0.2.0 LANGUAGES CXX
        DESCRIPTION "Linux control for ARGB case lighting on Gigabyte RGB Fusion 2 USB controllers"
        HOMEPAGE_URL "https://github.com/simba77/cougar-rgb")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(COUGAR_BUILD_GUI "Build the Qt GUI" ON)
option(COUGAR_BUILD_TESTS "Build unit tests" ON)

# Пути к бинарникам вшиваются в systemd-юнит и ярлык, поэтому по умолчанию ставим как пакет — в /usr.
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX /usr CACHE PATH "Install prefix" FORCE)
endif()
include(GNUInstallDirs)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SYSTEMD REQUIRED IMPORTED_TARGET libsystemd)
pkg_check_modules(PULSE REQUIRED IMPORTED_TARGET libpulse)
find_package(nlohmann_json 3.10 REQUIRED)
find_package(Threads REQUIRED)

add_compile_options(-Wall -Wextra -Wpedantic -Wno-missing-field-initializers)

configure_file(src/core/version.hpp.in ${CMAKE_BINARY_DIR}/generated/version.hpp @ONLY)

add_library(cougar_core STATIC
    src/core/audio.cpp
    src/core/clock.cpp
    src/core/color.cpp
    src/core/config.cpp
    src/core/device.cpp
    src/core/effects.cpp
    src/core/engine.cpp
    src/core/paths.cpp
    src/core/sink.cpp
)
target_include_directories(cougar_core PUBLIC src/core ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(cougar_core PUBLIC nlohmann_json::nlohmann_json PkgConfig::PULSE Threads::Threads)

add_executable(cougar-rgb
    src/cli/main.cpp
    src/cli/daemon.cpp
    src/cli/install.cpp
)
target_link_libraries(cougar-rgb PRIVATE cougar_core PkgConfig::SYSTEMD)
install(TARGETS cougar-rgb RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(COUGAR_BUILD_GUI)
    find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets)
    qt_standard_project_setup()
    qt_add_executable(cougar-rgb-gui
        src/gui/main.cpp
        src/gui/main_window.cpp
        src/gui/main_window.hpp
        src/gui/fan_preview.cpp
        src/gui/fan_preview.hpp
    )
    target_link_libraries(cougar-rgb-gui PRIVATE cougar_core Qt6::Widgets)
    install(TARGETS cougar-rgb-gui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

if(COUGAR_BUILD_TESTS)
    find_package(Catch2 3 REQUIRED)
    include(CTest)
    include(Catch)
    add_executable(cougar-tests
        tests/test_audio.cpp
        tests/test_config.cpp
        tests/test_device.cpp
        tests/test_effects.cpp
        tests/test_engine.cpp
    )
    target_link_libraries(cougar-tests PRIVATE cougar_core Catch2::Catch2WithMain)
    catch_discover_tests(cougar-tests)
endif()

install(FILES packaging/60-rgb-fusion2.rules DESTINATION lib/udev/rules.d)
configure_file(packaging/cougar-rgb.service.in ${CMAKE_BINARY_DIR}/cougar-rgb.service @ONLY)
configure_file(packaging/cougar-rgb.desktop.in ${CMAKE_BINARY_DIR}/cougar-rgb.desktop @ONLY)
install(FILES ${CMAKE_BINARY_DIR}/cougar-rgb.service DESTINATION lib/systemd/user)
install(FILES ${CMAKE_BINARY_DIR}/cougar-rgb.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES LICENSE README.md DESTINATION ${CMAKE_INSTALL_DOCDIR})

include(packaging/CPackConfig.cmake)
