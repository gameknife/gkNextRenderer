file(GLOB_RECURSE truckerDemoSources CONFIGURE_DEPENDS "*.cpp" "*.hpp" "*.h")

add_executable(TruckerDemo ${truckerDemoSources} ${GK_DESKTOP_MAIN_SOURCE})
gk_configure_application(TruckerDemo MODULES
    ${GK_STANDARD_RUNTIME_MODULES}
    ScadLoader
)
