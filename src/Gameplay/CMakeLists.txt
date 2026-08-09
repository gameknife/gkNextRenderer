add_library(NextGameplay ${src_files_nextgameplay})

gk_apply_target_defaults(NextGameplay)
gk_enable_unity_build(NextGameplay 8)
set_target_properties(NextGameplay PROPERTIES FOLDER "Gameplay")
target_link_libraries(NextGameplay PRIVATE gkNextEngine)

if(TARGET ScadLoader)
    gk_target_runtime_modules(NextGameplay MODULES ScadLoader)
endif()
if(TARGET NextAI)
    target_link_libraries(NextGameplay PRIVATE NextAI)
endif()
