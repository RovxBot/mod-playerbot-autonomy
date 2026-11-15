# Scripts are collected by the target playerbots AzerothCore module build.
# The core automatically discovers and installs conf/*.conf.dist from every
# enabled module (modules/CMakeLists.txt -> CopyModuleConfig).
# Do not call deprecated AC_ADD_CONFIG_FILE: it is a no-op in that core.
AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src")
