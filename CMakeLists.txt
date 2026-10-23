cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

# MatchFrame Multiview needs the frontend API (Studio Mode, Tools menu, events) and Qt.
option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)
option(MF_ENABLE_SELFTEST "Build the in-OBS automated self-test (runs only when MATCHFRAME_SELFTEST_OUT is set)" OFF)
option(MF_BUILD_TESTS "Build the OBS-independent unit tests" OFF)

if(NOT ENABLE_FRONTEND_API OR NOT ENABLE_QT)
  message(FATAL_ERROR "MatchFrame Multiview requires ENABLE_FRONTEND_API=ON and ENABLE_QT=ON")
endif()

include(compilerconfig)
include(defaults)
include(helpers)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(libobs REQUIRED)
find_package(obs-frontend-api REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

# --- OBS-independent core (layout, switching and playback decisions, config) ---------
add_library(matchframe-core STATIC)
target_sources(
  matchframe-core
  PRIVATE
    src/core/config.cpp
    src/core/config.hpp
    src/core/layout.cpp
    src/core/layout.hpp
    src/core/playback-logic.cpp
    src/core/playback-logic.hpp
    src/core/switch-logic.cpp
    src/core/switch-logic.hpp
)
set_target_properties(matchframe-core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(matchframe-core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
# config.cpp uses only libobs' obs_data API (works without a running OBS).
target_link_libraries(matchframe-core PUBLIC OBS::libobs)

# --- the plugin module ------------------------------------------------------------------
add_library(${CMAKE_PROJECT_NAME} MODULE)

target_link_libraries(
  ${CMAKE_PROJECT_NAME}
  PRIVATE matchframe-core OBS::libobs OBS::obs-frontend-api Qt6::Core Qt6::Gui Qt6::Widgets
)

# Wayland native surface lookup needs Qt's QPA interface (private headers).
if(UNIX AND NOT APPLE)
  find_package(Qt6 QUIET COMPONENTS GuiPrivate)
  if(TARGET Qt6::GuiPrivate)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::GuiPrivate)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE MF_HAVE_QPA)
  else()
    message(WARNING "Qt6 GuiPrivate not found: the multiview display will not work on native Wayland sessions")
  endif()
endif()

target_compile_options(
  ${CMAKE_PROJECT_NAME}
  PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
)
set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON)

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/controller.cpp
    src/controller.hpp
    src/graphics-controller.cpp
    src/graphics-controller.hpp
    src/multiview-window.cpp
    src/multiview-window.hpp
    src/obs-util.cpp
    src/obs-util.hpp
    src/playback-controller.cpp
    src/playback-controller.hpp
    src/plugin-main.cpp
    src/render-surface.cpp
    src/render-surface.hpp
    src/scene-bank.cpp
    src/scene-bank.hpp
    src/setup-dialog.cpp
    src/setup-dialog.hpp
    src/switch-controller.cpp
    src/switch-controller.hpp
)

if(MF_ENABLE_SELFTEST)
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/selftest.cpp src/selftest.hpp)
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE MF_ENABLE_SELFTEST)
endif()

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

# --- unit tests ---------------------------------------------------------------------------
if(MF_BUILD_TESTS)
  enable_testing()
  add_executable(matchframe-unit-tests tests/unit/test-main.cpp)
  target_link_libraries(matchframe-unit-tests PRIVATE matchframe-core)
  add_test(NAME matchframe-unit-tests COMMAND matchframe-unit-tests)
endif()
