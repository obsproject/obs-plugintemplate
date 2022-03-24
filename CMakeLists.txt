# --- Detect if the plugin is build out of tree or not ---
if(CMAKE_PROJECT_NAME STREQUAL "obs-studio")
  set(BUILD_OUT_OF_TREE OFF)
else()
  set(BUILD_OUT_OF_TREE ON)
  cmake_minimum_required(VERSION 3.16)
endif()
# --- End of section ---

# --- General plugin settings ---

# Change obs-plugintemplate to your plugin's name in a machine-readable format
# (e.g.: obs-myawesomeplugin) and set
project(obs-plugintemplate VERSION 1.0.5)
add_library(${PROJECT_NAME} MODULE)

# Replace `Your Name Here` with the name (yours or your organization's) you want
# to see as the author of the plugin (in the plugin's metadata itself and in the
# installers)
set(PLUGIN_AUTHOR "Your Name Here")

# Replace `com.example.obs-plugin-template` with a unique Bundle ID for macOS
# releases (used both in the installer and when submitting the installer for
# notarization)
set(MACOS_BUNDLEID "com.example.${PROJECT_NAME}")

# Replace `me@contoso.com` with the maintainer email address you want to put in
# Linux packages
set(LINUX_MAINTAINER_EMAIL "me@mymailhost.com")

# --- End of section ---

# --- Plugins sources settings

# Add your custom source files here - header files are optional and only
# required for visibility e.g. in Xcode or Visual Studio
target_sources(${PROJECT_NAME} PRIVATE src/plugin-main.c)

# --- End of section ---

# --- Library linking settings ---

if(BUILD_OUT_OF_TREE)
  find_package(libobs REQUIRED)
  find_package(obs-frontend-api REQUIRED)
  include(cmake/ObsPluginHelpers.cmake)
  set(OBS_FRONTEND_API_NAME "obs-frontend-api")
else()
  set(OBS_FRONTEND_API_NAME "frontend-api")
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE OBS::libobs)

# Uncomment this line if you want to use the OBS Frontend API in your plugin
# target_link_libraries(${PROJECT_NAME} PRIVATE OBS::${OBS_FRONTEND_API_NAME})

# Uncomment those lines if you want add Qt UI in your plugin

# find_qt(VERSION ${QT_VERSION} COMPONENTS Widgets Core)

# target_link_libraries(${PROJECT_NAME} PRIVATE Qt::Core Qt::Widgets)

# set_target_properties(${PROJECT_NAME} PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC
# ON)

# --- End of section ---

# /!\ TAKE NOTE: No need to edit things past this point /!\

configure_file(src/plugin-macros.h.in
               ${CMAKE_CURRENT_BINARY_DIR}/src/plugin-macros.generated.h)

target_sources(
  ${PROJECT_NAME}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/src/plugin-macros.generated.h)

# --- Platform-independent build settings ---

target_include_directories(
  ${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
                          ${CMAKE_CURRENT_BINARY_DIR}/src)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)

# --- End of section ---

if(BUILD_OUT_OF_TREE)
  # --- Windows-specific build settings and tasks ---
  if(OS_WINDOWS)
    configure_file(cmake/bundle/windows/installer-Windows.iss.in
                   ${CMAKE_CURRENT_BINARY_DIR}/installer-Windows.generated.iss)

    if(MSVC)
      target_compile_options(${PROJECT_NAME} PRIVATE /MP /d2FH4-)
    endif()
    # --- End of section ---

    # -- macOS specific build settings and tasks --
  elseif(OS_MACOS)
    configure_file(
      cmake/bundle/macos/installer-macOS.pkgproj.in
      ${CMAKE_CURRENT_BINARY_DIR}/installer-macOS.generated.pkgproj)

    set(MACOSX_PLUGIN_GUI_IDENTIFIER "${MACOS_BUNDLEID}")
    set(MACOSX_PLUGIN_BUNDLE_VERSION "${CMAKE_PROJECT_VERSION}")
    set(MACOSX_PLUGIN_SHORT_VERSION_STRING "1")

    target_compile_options(
      ${PROJECT_NAME}
      PRIVATE -Wall -Wextra -Werror-implicit-function-declaration
              -stdlib=libc++ -fvisibility=default)

    set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "")
    # --- End of section ---

    # --- Linux-specific build settings and tasks ---
  else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra)

    set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "")
  endif()
  # --- End of section ---

  # --- In-tree build settings and tasks ---
else()
  set_target_properties(${PROJECT_NAME} PROPERTIES "plugins" PREFIX "")
  # --- End of section ---
endif()

setup_plugin_target(${PROJECT_NAME})
