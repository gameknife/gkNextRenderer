file(GLOB_RECURSE truckerDemoSources CONFIGURE_DEPENDS "*.cpp" "*.hpp" "*.h")

gk_add_application(TruckerDemo
    SOURCES ${truckerDemoSources}
    MODULES
        ${GK_STANDARD_RUNTIME_MODULES}
        ScadLoader
)
