# A console tool with its own main(), not an engine application: no SDL entry point, no modules.
gk_add_application(Packager NO_UNITY NO_FAST_LINK NO_DEFAULT_MAIN
    SOURCES PackagerMain.cpp
)
