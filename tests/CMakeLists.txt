# TinyServe unit tests.
# Each test file is a standalone executable with its own main().
# No third-party test frameworks — plain C + assert() + CTest.

# Common sources needed by the tests (pulled from src/).
# Each test links only what it needs; list them per-target below.

function(ts_add_test name)
    set(multi SOURCES)
    cmake_parse_arguments(TS "" "" "${multi}" ${ARGN})
    add_executable(${name} ${TS_SOURCES})
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${LIBUV_INCLUDE_DIRS}
    )
    target_link_libraries(${name} PRIVATE ${LIBUV_LIBRARIES})
    if(LIBUV_LIBRARY_DIRS)
        target_link_directories(${name} PRIVATE ${LIBUV_LIBRARY_DIRS})
    endif()
    target_compile_options(${name} PRIVATE
        -Wall -Wextra -Wpedantic -Wno-unused-parameter
        # Tests rely on assert() side effects (the only checking
        # mechanism we use). Make sure NDEBUG is never defined here,
        # even when CMAKE_BUILD_TYPE=Release adds -DNDEBUG, otherwise
        # every assertion silently turns into a no-op.
        -UNDEBUG
    )
    add_test(NAME ${name} COMMAND ${name})
endfunction()

ts_add_test(test_smoke SOURCES test_smoke.c)
ts_add_test(test_config SOURCES
    test_config.c
    ${CMAKE_SOURCE_DIR}/src/config.c
    ${CMAKE_SOURCE_DIR}/src/log.c
)
ts_add_test(test_path_utils SOURCES
    test_path_utils.c
    ${CMAKE_SOURCE_DIR}/src/path_utils.c
)
ts_add_test(test_range SOURCES
    test_range.c
    ${CMAKE_SOURCE_DIR}/src/range.c
    ${CMAKE_SOURCE_DIR}/src/log.c
)
ts_add_test(test_http_parser SOURCES
    test_http_parser.c
    ${CMAKE_SOURCE_DIR}/src/http_parser.c
    ${CMAKE_SOURCE_DIR}/src/log.c
)
