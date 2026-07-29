foreach(gkModule IN LISTS GK_MODULE_NAMES)
    set(moduleSources ${src_files_module_${gkModule}})
    if(moduleSources)
        add_library(${gkModule} STATIC ${moduleSources})
        gk_configure_module(${gkModule})
    endif()
endforeach()

if(TARGET LiveCoding AND GK_ENABLE_CPP_LIVE_CODING)
    target_include_directories(LiveCoding SYSTEM PRIVATE
        "${GK_LIVEPP_RESOLVED_ROOT}/API/x64"
    )
    target_compile_definitions(LiveCoding PRIVATE
        GK_LIVEPP_ROOT="${GK_LIVEPP_RESOLVED_ROOT}"
    )
endif()

if(TARGET GltfLoader)
    set_source_files_properties(
        ${GK_SOURCE_ROOT}/Modules/GltfLoader/FSceneLoader.cpp
        PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON
    )
endif()

if(TARGET SceneExport)
    set_source_files_properties(
        ${GK_SOURCE_ROOT}/Modules/SceneExport/FSceneSaver.cpp
        PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON
    )
endif()

if(TARGET ScadLoader)
    target_link_libraries(ScadLoader PRIVATE xxHash::xxhash)

    find_package(manifold CONFIG QUIET)
    if(manifold_FOUND AND NOT GK_DISABLE_MANIFOLD)
        set_target_properties(manifold::manifold PROPERTIES
            INTERFACE_COMPILE_FEATURES ""
            INTERFACE_COMPILE_OPTIONS ""
        )
        target_link_libraries(ScadLoader PRIVATE manifold::manifold)
        target_compile_definitions(ScadLoader PRIVATE
            GK_WITH_MANIFOLD=1
            MANIFOLD_CROSS_SECTION
            MANIFOLD_PAR=-1
        )
        message(STATUS "Manifold found: SCAD CSG boolean backend enabled")
    else()
        target_compile_definitions(ScadLoader PRIVATE GK_WITH_MANIFOLD=0)
        message(STATUS "Manifold not found: SCAD CSG falls back to first-child approximation")
    endif()

    find_package(Freetype QUIET)
    if(Freetype_FOUND AND NOT GK_DISABLE_SCAD_TEXT)
        if(TARGET freetype)
            target_link_libraries(ScadLoader PRIVATE freetype)
        else()
            target_link_libraries(ScadLoader PRIVATE Freetype::Freetype)
        endif()
        target_compile_definitions(ScadLoader PRIVATE GK_WITH_FREETYPE=1)
        message(STATUS "FreeType found: SCAD text() enabled")
    else()
        target_compile_definitions(ScadLoader PRIVATE GK_WITH_FREETYPE=0)
        message(STATUS "FreeType not found: SCAD text() skipped")
    endif()

    target_compile_definitions(ScadLoader PRIVATE
        GK_WITH_EARCUT=$<NOT:$<BOOL:${IOS}>>
    )
endif()

if(TARGET SplatLoader)
    find_package(WebP CONFIG REQUIRED)
    find_package(ZLIB REQUIRED)
    target_link_libraries(SplatLoader PRIVATE WebP::webp ZLIB::ZLIB)
endif()

if(TARGET NextQuickJS)
    target_compile_definitions(NextQuickJS PRIVATE
        GK_NEXT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )
    target_link_libraries(NextQuickJS PRIVATE quickjs SDL3::SDL3 imgui::imgui)
endif()

if(TARGET NextPhysics)
    find_package(Jolt CONFIG REQUIRED)
    target_include_directories(NextPhysics PRIVATE
        $<TARGET_PROPERTY:Jolt::Jolt,INTERFACE_INCLUDE_DIRECTORIES>
    )
    target_link_libraries(NextPhysics PRIVATE Jolt::Jolt)
endif()

if(TARGET NextRmlUi)
    find_package(RmlUi CONFIG REQUIRED)
    target_link_libraries(NextRmlUi PRIVATE RmlUi::RmlUi)
endif()

if(TARGET NextStreamline AND WITH_STREAMLINE)
    target_compile_definitions(NextStreamline PRIVATE
        GK_STREAMLINE_INTERPOSER_DLL="${STREAMLINE_BIN_DIR}/sl.interposer.dll"
    )
    target_include_directories(NextStreamline PRIVATE ${STREAMLINE_INCLUDE_DIR})
    target_link_directories(NextStreamline PRIVATE ${STREAMLINE_LIB_DIR})
endif()

if(TARGET NextFidelityFX)
    target_compile_definitions(NextFidelityFX PRIVATE
        WITH_FIDELITYFX=$<BOOL:${WITH_FIDELITYFX}>
    )
    if(WITH_FIDELITYFX)
        target_include_directories(NextFidelityFX PRIVATE ${FIDELITYFX_INCLUDE_DIR})
        target_link_libraries(NextFidelityFX PRIVATE ${FIDELITYFX_LIB})
        file(COPY ${FIDELITYFX_DLL} DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
    endif()
endif()

if(TARGET NextRemote)
    if(TARGET RenderViews)
        target_link_libraries(NextRemote PRIVATE RenderViews)
    endif()
    target_link_libraries(NextRemote PRIVATE NextGameplay)
    if(GK_REMOTE_ENABLED)
        target_link_libraries(NextRemote PRIVATE ${GK_REMOTE_LINK_LIBS})
    endif()
endif()
