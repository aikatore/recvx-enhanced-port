# ctest "settings": every recvx.ini key in config.c (keys[]) is edited by a row of the settings
# table (settings.c g_settings[]), which both menus show (F1 and OPTION > ENHANCED SETTINGS), or
# is listed here with where it is set instead. A new setting without a menu row fails.
#   cmake -DSRC=<pc dir> -P check_settings.cmake
set(NOT_IN_TABLE
    coop_items   # lobby ITEMS / F3 "Items when joining": the joiner's save list, chosen per session
    coop_start   # lobby START: the host's save to start from, chosen per session
    biorand      # lobby GAME RULES "BIORAND" (--dev): the co-op host's new game plays BioRand (biorand.h; no longer a setting)
    tps_key      # CONTROLS bindings, "Third person on/off" (bindings.c, its first keyboard slot)
    borderless_extra   # ini only: the Windows borderless window's overhang past the monitor (settings.c), a driver workaround
)
file(READ "${SRC}/src/host/config.c" config)
file(READ "${SRC}/src/host/settings.c" table)   # rows, and the get/set helpers of rows such as Window size
string(REGEX MATCHALL "{ \"[a-z_0-9]+\", (\\(int\\*\\))?&[A-Za-z_0-9.]+" entries "${config}")
list(LENGTH entries n)
if(n LESS 40)
    message(FATAL_ERROR "settings: only ${n} ini keys found in config.c; update this check")
endif()
set(missing "")
foreach(e ${entries})
    string(REGEX REPLACE "{ \"([a-z_0-9]+)\".*" "\\1" key "${e}")
    string(REGEX REPLACE ".*&([A-Za-z_.]+)" "\\1" var "${e}")
    list(FIND NOT_IN_TABLE ${key} skip)
    string(REPLACE "." "\\." re "${var}")
    string(REGEX MATCH "[^A-Za-z_0-9.]${re}[^A-Za-z_0-9]" found "${table}")
    if(skip EQUAL -1 AND NOT found)
        list(APPEND missing "${key} (${var})")
    endif()
endforeach()
if(missing)
    message(FATAL_ERROR "settings: ini keys without a row in g_settings[] (pc/src/host/settings.c), so missing from "
                        "F1 and ENHANCED SETTINGS: ${missing}. Add a row, or list the key in ${CMAKE_CURRENT_LIST_FILE} with where it is set.")
endif()
message(STATUS "settings: ${n} ini keys, all in the settings table or listed")
