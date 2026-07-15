include(cmake/RuntimeAssets.cmake)
include(cmake/SlangShaders.cmake)

add_custom_target(gkNextShaders
    DEPENDS ${all_shader_outputs}
    SOURCES ${shader_files} ${shader_common_files}
)

add_custom_target(Assets
    DEPENDS ${all_asset_stamps}
    SOURCES ${all_asset_files}
)

add_dependencies(Assets gkNextShaders)
set_target_properties(gkNextShaders PROPERTIES FOLDER "Shaders")
