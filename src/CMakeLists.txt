
find_package( PkgConfig REQUIRED )
pkg_check_modules( GEANY REQUIRED geany )
pkg_get_variable( PLUGIN_DIR geany libdir )

add_library( bracketcolors SHARED
    bracketcolors.cc
    BracketMap.cc
)

add_definitions( ${GEANY_CFLAGS} )
target_compile_features(bracketcolors PRIVATE cxx_std_17)

target_link_libraries( bracketcolors PUBLIC
    ${GEANY_LIBRARIES}
)

target_include_directories( bracketcolors PUBLIC
    ${GEANY_INCLUDE_DIRS}
)

# strip the lib prefix so we are recognized by the geany plugin system
set_target_properties(
    bracketcolors PROPERTIES PREFIX ""
)

install(
  TARGETS bracketcolors
  LIBRARY DESTINATION "${PLUGIN_DIR}/geany/"
  COMPONENT runtime
)
