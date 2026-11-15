# AzerothCore discovers sources in this module's src/ directory.
# The module is deliberately inert until the implementation milestones land.
# mod-playerbots and the playerbots core fork are future runtime dependencies;
# no hard-coded source paths or cross-module symbols are introduced here.

AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src")
AC_ADD_CONFIG_FILE("${CMAKE_CURRENT_LIST_DIR}/conf/mod_playerbot_autonomy.conf.dist")
