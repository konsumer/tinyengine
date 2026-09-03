# The demo, and the headless renderer that checks it.

set(DEMO_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# Emscripten for the web, raylib for the desktop. The game source is identical either way.
if(EMSCRIPTEN)
    add_executable(demo ${DEMO_DIR}/demo.c)
    target_compile_definitions(demo PRIVATE PNTR_APP_WEB)
    target_link_libraries(demo PRIVATE pntr pntr_app tinyengine)
    set_target_properties(demo PROPERTIES SUFFIX ".html")
    target_link_options(demo PRIVATE
        -sALLOW_MEMORY_GROWTH
        -sSINGLE_FILE
        --shell-file ${DEMO_DIR}/shell.html
        -sSINGLE_FILE=1
    )
    set_target_properties(demo PROPERTIES OUTPUT_NAME "index")
elseif(TINYENGINE_BUILD_EXAMPLE_RAYLIB)
    find_package(raylib REQUIRED)
    add_executable(demo ${DEMO_DIR}/demo.c)
    target_compile_definitions(demo PRIVATE PNTR_APP_RAYLIB)
    target_link_libraries(demo PRIVATE raylib pntr pntr_app tinyengine)
endif()

if(TARGET demo)
    target_include_directories(demo PRIVATE ${DEMO_DIR})
    set_property(TARGET demo PROPERTY C_STANDARD 11)
    target_compile_options(demo PRIVATE -Wall -Wextra)
endif()

# The headless renderer needs no backend at all, so it always builds.
if(TINYENGINE_BUILD_PREVIEW)
    add_executable(tiny_preview ${CMAKE_SOURCE_DIR}/tools/tiny_preview.c)
    target_include_directories(tiny_preview PRIVATE ${DEMO_DIR})
    target_link_libraries(tiny_preview PRIVATE pntr pntr_app tinyengine)
    set_property(TARGET tiny_preview PROPERTY C_STANDARD 11)
    target_compile_options(tiny_preview PRIVATE -Wall -Wextra)
endif()
