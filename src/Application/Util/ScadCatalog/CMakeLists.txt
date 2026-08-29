# A console tool with its own main(); it uses ScadLoader without ever opening a window.
gk_add_application(ScadCatalog NO_DEFAULT_MAIN
    SOURCES
        ScadCatalogMain.cpp
        ${GK_SOURCE_ROOT}/Application/Editor/ScadLibrary/KitCatalog.cpp
    MODULES ScadLoader
)
