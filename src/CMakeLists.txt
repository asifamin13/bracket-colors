
find_package( PkgConfig REQUIRED )
pkg_check_modules( GEANY REQUIRED geany )
pkg_get_variable( PLUGIN_DIR geany libdir )

add_library( bracket-colors SHARED
    bracket-colors.cc
    BracketMap.cc
)

add_definitions( ${GEANY_CFLAGS} )
target_compile_features(bracket-colors PRIVATE cxx_std_17)

target_link_libraries( bracket-colors PUBLIC
    ${GEANY_LIBRARIES}
)

target_include_directories( bracket-colors PUBLIC
    ${GEANY_INCLUDE_DIRS}
)

# strip the lib prefix so we are recognized by the geany plugin system
set_target_properties(
    bracket-colors PROPERTIES PREFIX ""
)

install(
  TARGETS bracket-colors
  LIBRARY DESTINATION "${PLUGIN_DIR}/geany/"
  COMPONENT runtime
)
