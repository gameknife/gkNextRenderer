add_executable(ScadCatalog
    ScadCatalogMain.cpp
    ${GK_SOURCE_ROOT}/Application/Editor/ScadLibrary/KitCatalog.cpp
)
gk_configure_application(ScadCatalog MODULES
    ScadLoader
)
