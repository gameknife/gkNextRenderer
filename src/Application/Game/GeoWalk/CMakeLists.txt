file(GLOB_RECURSE geoWalkSources CONFIGURE_DEPENDS "*.cpp" "*.hpp" "*.h")

add_executable(GeoWalk ${geoWalkSources} ${GK_DESKTOP_MAIN_SOURCE})
gk_configure_application(GeoWalk MODULES
    ${GK_STANDARD_RUNTIME_MODULES}
    ScadLoader
)
target_link_libraries(GeoWalk PRIVATE NextGameplay)
