# CTest entries that render every screen layout with pinball_snapshot, and
# that hold its command line and the layout dump to account.
# Included from CMakeLists.txt inside if(BUILD_TESTING), after the render
# tests, with PINBALL_BUILD_TOOLS, PINBALL_TEST_DIR and PINBALL_HEADLESS_ENV
# set. The pictures are left in PINBALL_TEST_DIR as layout_*.png, to be
# looked at or uploaded.
if(NOT PINBALL_BUILD_TOOLS)
  return()
endif()

# One render: layout_<name>.png from the given state and options. The tool
# itself fails if the table's part of the frame is blank.
function(pinball_layout_render name steps state)
  add_test(NAME layout_${name}
    COMMAND pinball_snapshot ${steps} layout_${name}.png ${state} ${ARGN}
    WORKING_DIRECTORY ${PINBALL_TEST_DIR})
  set_tests_properties(layout_${name} PROPERTIES
    ENVIRONMENT "${PINBALL_HEADLESS_ENV}" TIMEOUT 60 LABELS layout)
endfunction()

# Every preset, ten seconds into a game.
foreach(preset IN ITEMS desktop phone phone-pro phone-short phone-max duo-outer duo-inner
                        pad-portrait pad-portrait-11 pad-split pad-landscape pad-landscape-11)
  string(REPLACE "-" "_" name "${preset}")
  pinball_layout_render(${name} 1200 play --layout ${preset})
endforeach()

# The same again as a check: the tool draws every screen of the layout and
# fails unless each control, each window of the status display and each
# dialog is really there, changes when it should (pressed, paused, each
# About page) and stays put when it should not. A drawer that drew nothing
# would pass the renders above; it does not pass these.
foreach(preset IN ITEMS desktop phone phone-pro phone-short phone-max duo-outer duo-inner
                        pad-portrait pad-portrait-11 pad-split pad-landscape pad-landscape-11
                        352x602 700x602)
  string(REGEX REPLACE "[^a-z0-9]" "_" name "${preset}")
  pinball_layout_render(check_${name} 1200 check --layout ${preset})
endforeach()

# Every state on one Stack and one Classic touch layout; the pages only the
# desktop draws its own way on the desktop as well.
foreach(preset IN ITEMS phone-pro pad-landscape)
  string(REPLACE "-" "_" name "${preset}")
  foreach(state IN ITEMS launch pause interrupted confirm about about-info licences initials
                         gameover course multiball)
    string(REPLACE "-" "_" state_name "${state}")
    pinball_layout_render(${name}_${state_name} 60 ${state} --layout ${preset})
  endforeach()
endforeach()
foreach(state IN ITEMS pause about-info licences gameover course)
  string(REPLACE "-" "_" state_name "${state}")
  pinball_layout_render(desktop_${state_name} 60 ${state})
endforeach()
pinball_layout_render(phone_short_pressed 1200 play --layout phone-short --zones
  --pressed left,right,centre,nl,nr,pause)
pinball_layout_render(phone_pro_device_pixels 60 launch --layout phone-pro@3)
pinball_layout_render(phone_longest_message 60 course --layout phone
  --message "A MESSAGE OF THE GREATEST LENGTH: 36")
pinball_layout_render(phone_last_licence_page 60 licences --layout phone --page 99)

# Any window: twelve shapes from 0.40 to 2.2 wide for their height, with
# and without insets, down to the smallest scene and a little under it.
set(layout_sweep
  "400x1000" "352x602" "402x874+62,34,0,0" "375x667+20,0,0,0" "500x800" "600x800+24,20,0,0"
  "700x800" "780x800" "800x800+0,21,62,62" "1000x800" "1280x800+24,20,0,0" "1100x500" "300x500")
foreach(surface IN LISTS layout_sweep)
  string(REGEX REPLACE "[^0-9x]" "_" name "${surface}")
  pinball_layout_render(any_${name} 60 launch --layout ${surface})
endforeach()
pinball_layout_render(any_surface_option 60 launch --surface 820x1180 --insets 24,20,0,0
  --idiom touch)

# A command line that is not understood is refused, with the usage text and
# nothing drawn.
set(layout_bad_args
  "no_arguments:"
  "bad_steps:0|x.png"
  "scale_too_large_for_window:60|x.png|--layout|4096x4096@4"
  "bad_name:60|../x.png"
  "unknown_state:60|x.png|paused"
  "unknown_option:60|x.png|--bogus"
  "unknown_preset:60|x.png|--layout|phone-mini"
  "layout_without_value:60|x.png|--layout"
  "layout_too_small:60|x.png|--layout|99x800"
  "layout_too_large:60|x.png|--layout|800x4097"
  "layout_trailing_junk:60|x.png|--layout|800x600x"
  "layout_negative:60|x.png|--layout|-800x600"
  "layout_bad_scale:60|x.png|--layout|800x600@5"
  "layout_bad_insets:60|x.png|--layout|800x600+1,2,3"
  "layout_insets_too_large:60|x.png|--layout|800x600+300,300,0,0"
  "layout_twice:60|x.png|--layout|phone|--layout|phone"
  "layout_and_surface:60|x.png|--layout|phone|--surface|800x600"
  "surface_is_preset:60|x.png|--surface|phone"
  "insets_without_surface:60|x.png|--insets|1,2,3,4"
  "bad_idiom:60|x.png|--idiom|tablet"
  "bad_page:60|x.png|licences|--page|0"
  "bad_message:60|x.png|--message|lower case"
  "long_message:60|x.png|--message|AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
  "bad_pressed:60|x.png|--pressed|left,up"
  "pressed_twice:60|x.png|--pressed|left,left"
  "state_after_option:60|x.png|--zones|about"
  "two_states:60|x.png|about|initials")
foreach(case IN LISTS layout_bad_args)
  string(REGEX MATCH "^[^:]+" name "${case}")
  string(REGEX REPLACE "^[^:]+:" "" args "${case}")
  add_test(NAME layout_rejects_${name}
    COMMAND ${CMAKE_COMMAND} -DPINBALL=$<TARGET_FILE:pinball_snapshot> "-DARGS=${args}"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/ExpectUsageError.cmake)
  set_tests_properties(layout_rejects_${name} PROPERTIES
    WORKING_DIRECTORY ${PINBALL_TEST_DIR} TIMEOUT 30 LABELS layout)
endforeach()

# The licence notices on the About screen say what the licence files say:
# this repository's always, and those of the pinned libraries when their
# sources are in the build tree (PINBALL_FETCH_DEPS).
if(Python3_Interpreter_FOUND)
  set(layout_licences ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE)
  foreach(candidate IN ITEMS "${box2d_SOURCE_DIR}/LICENSE" "${sdl3_SOURCE_DIR}/LICENSE.txt"
                             "${SDL3_SOURCE_DIR}/LICENSE.txt")
    if(EXISTS "${candidate}" AND NOT IS_DIRECTORY "${candidate}")
      list(APPEND layout_licences "${candidate}")
    endif()
  endforeach()
  list(REMOVE_DUPLICATES layout_licences)
  add_test(NAME layout_notices
    COMMAND Python3::Interpreter -I ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_notices.py
            ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/Notices.hpp ${layout_licences})
  set_tests_properties(layout_notices PROPERTIES TIMEOUT 30 LABELS layout)
endif()

# The layout dump carries the screen layouts, and they say what the header
# says: the playfield is the table in every preset, the canvas is the window
# at the scale given, and every touch control is in the canvas.
if(Python3_Interpreter_FOUND)
  add_test(NAME layout_json_screen
    COMMAND Python3::Interpreter -I ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_layout_json.py
            $<TARGET_FILE:pinball_layout>)
  set_tests_properties(layout_json_screen PROPERTIES TIMEOUT 30 LABELS layout)
endif()
