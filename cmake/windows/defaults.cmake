# CMake Windows defaults module

include_guard(GLOBAL)

# Enable find_package targets to become globally available targets
set(CMAKE_FIND_PACKAGE_TARGETS_GLOBAL TRUE)

include(buildspec)

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  file(TO_CMAKE_PATH "$ENV{ALLUSERSPROFILE}/obs-studio/plugins" _default_install_prefix)
  set(
    CMAKE_INSTALL_PREFIX
    "${_default_install_prefix}"
    CACHE STRING
    "Default plugin installation directory"
    FORCE
  )
  unset(_default_install_prefix)
endif()