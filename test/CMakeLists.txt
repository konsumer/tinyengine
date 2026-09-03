# Renders every map type headlessly and checks what came out.
#
# Each case asserts the frame is not blank, that sprites were on screen, and -- the one that
# actually catches collision regressions -- that the player did not come to rest inside a
# solid tile. Walking a direction into a wall for 400 frames is what finds that.
if(NOT TARGET tiny_preview)
    return()
endif()

set(TINY_PREVIEW_MAPS town cave ledge arena pyramid sky)
set(TINY_PREVIEW_DIRS left right up down left,down right,up)

foreach(map ${TINY_PREVIEW_MAPS})
    # A short run with no input, kept as the frame that gets written out for inspection.
    add_test(NAME preview_${map}
        COMMAND tiny_preview --map ${map} --frames 40
                --out ${CMAKE_CURRENT_BINARY_DIR}/preview_${map}.png
    )
    # Longer runs held against the level geometry, to find anything that clips through it.
    foreach(dir ${TINY_PREVIEW_DIRS})
        string(REPLACE "," "_" tag ${dir})
        add_test(NAME walk_${map}_${tag}
            COMMAND tiny_preview --map ${map} --frames 400 --hold ${dir} --out /dev/null
        )
    endforeach()
endforeach()
