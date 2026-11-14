# The table's acceptance tests (TABLE.md section 5), included from the tests
# section of CMakeLists.txt.
#
# The quick part runs inside core_tests with every build. The statistical
# part is its own program, pinball_acceptance: it plays about half a billion
# simulation steps (about three quarters of an hour on two cores), so it is
# built with everything else, to keep it compiling, but its tests are
# registered only when asked for:
#
#   cmake -B build -DPINBALL_ACCEPTANCE_TESTS=ON ...
#   ctest --test-dir build -L acceptance -j 2 --output-on-failure
#
# or run directly, for the whole report in one table:
#
#   build/pinball_acceptance | grep '^ROW'
#
# Use an optimized build without sanitizers for it. A FAIL line (a fairness
# or structural criterion not met, or a known miss gone beyond its pin)
# fails the test; a KNOWN-MISS line (a pacing criterion the design is known
# to miss, no worse than the value recorded for it plus a margin) does not.
option(PINBALL_ACCEPTANCE_TESTS
       "Register the table's long acceptance tests with CTest (label: acceptance)" OFF)

add_executable(pinball_acceptance
  tests/AcceptanceMain.cpp
  tests/bots/Acceptance.cpp
  tests/bots/TableHarness.cpp
)
target_include_directories(pinball_acceptance PRIVATE tests)
target_link_libraries(pinball_acceptance PRIVATE pinball_core)

# No table of the layout is made with the C library's mathematics, and
# nothing that moves the ball calls its transcendental functions. (Sound and
# Music do, for the samples they make: those never reach the ball.)
set(trajectory_sources "")
foreach(name IN ITEMS Physics.cpp Physics.hpp Game.cpp Game.hpp Rules.cpp Rules.hpp
                      Missions.cpp Missions.hpp Vec2.hpp FixedTimestep.hpp Input.hpp)
  list(APPEND trajectory_sources ${CMAKE_SOURCE_DIR}/src/core/${name})
endforeach()
list(JOIN trajectory_sources "," trajectory_sources)
add_test(NAME table_layout_no_libm
  COMMAND ${CMAKE_COMMAND} -DHEADER=${CMAKE_SOURCE_DIR}/src/core/TableLayout.hpp
          -DSOURCES=${trajectory_sources}
          -P ${CMAKE_SOURCE_DIR}/tests/CheckNoLibm.cmake)

if(PINBALL_ACCEPTANCE_TESTS)
  # One test per section, so that they can run side by side. A section name
  # with a colon runs one part of it: one timing error, one bot.
  foreach(section IN ITEMS launch shots lanes exits rest wedge p1
                           p2:3 p2:4 p2:5 p2:6 p2:8 p2saver:3 p2saver:5 p2saver:8
                           kickback stress:p1 stress:p2)
    string(REPLACE ":" "_" section_id "${section}")
    add_test(NAME acceptance_${section_id} COMMAND pinball_acceptance ${section})
    set_tests_properties(acceptance_${section_id} PROPERTIES LABELS acceptance TIMEOUT 14400)
  endforeach()
endif()
