-- xmake build script for TinyServe
-- SPDX-License-Identifier: MIT
--
-- Mirrors the behaviour of the canonical CMake build:
--   * C11, -Wall -Wextra -Wpedantic
--   * Release adds _FORTIFY_SOURCE=2 + stack protector + PIE
--     (GNU-ld-only flags are gated to Linux)
--   * Same five ctest unit tests, each with NDEBUG forced off
--
-- Usage:
--   xmake f -m release          # configure (release by default)
--   xmake                       # build
--   xmake test                  # run unit tests
--   xmake install -o /usr/local # install binary + man page

set_project("tinyserve")
set_version("0.3.1")
set_xmakever("2.7.0")

set_languages("c11")
set_warnings("all", "extra", "pedantic")
add_cflags("-Wno-unused-parameter")

set_allowedmodes("debug", "release", "releasedbg")
set_defaultmode("release")
add_rules("mode.debug", "mode.release", "mode.releasedbg")

-- ── libuv ───────────────────────────────────────────────────────────────
-- Prefer the system package via pkg-config, otherwise fall back to
-- xmake-managed dependency resolution (xrepo). Either path exposes
-- the same `libuv` package alias to the targets below.
add_requires("pkgconfig::libuv", { alias = "libuv", optional = true })
if not has_config("libuv") then
    add_requires("libuv", { alias = "libuv" })
end

-- ── feature probes ──────────────────────────────────────────────────────
option("getrandom")
    set_default(false)
    set_showmenu(false)
    add_csnippets("getrandom",
        "#include <sys/random.h>\nint main(){char b;return getrandom(&b,1,0);}")
    add_defines("TS_HAVE_GETRANDOM=1")
option_end()

option("arc4random_buf")
    set_default(false)
    set_showmenu(false)
    add_csnippets("arc4random_buf",
        "#include <stdlib.h>\nint main(){char b;arc4random_buf(&b,1);return 0;}")
    add_defines("TS_HAVE_ARC4RANDOM_BUF=1")
option_end()

-- ── main binary ─────────────────────────────────────────────────────────
target("tinyserve")
    set_kind("binary")
    add_files("src/*.c")
    add_includedirs("src")
    add_packages("libuv")
    add_options("getrandom", "arc4random_buf")

    if is_plat("macosx") then
        add_defines("TS_PLATFORM_MACOS")
    elseif is_plat("linux") then
        add_defines("TS_PLATFORM_LINUX")
        add_syslinks("pthread", "dl")
    end

    -- Release-only hardening, mirroring CMakeLists.txt.
    if is_mode("release", "releasedbg") then
        add_cflags(
            "-D_FORTIFY_SOURCE=2",
            "-fstack-protector-strong",
            "-fPIE"
        )
        if is_plat("linux") then
            add_cflags("-fstack-clash-protection")
            add_ldflags(
                "-pie",
                "-Wl,-z,relro",
                "-Wl,-z,now",
                { force = true }
            )
        else
            add_ldflags("-pie", { force = true })
        end
    end

    -- Install layout matches `cmake --install`:
    --   <prefix>/bin/tinyserve
    --   <prefix>/share/man/man1/tinyserve.1
    set_installdir("$(prefix)")
    add_installfiles("man/tinyserve.1", { prefixdir = "share/man/man1" })
target_end()

-- ── unit tests ──────────────────────────────────────────────────────────
-- Each test is a standalone executable that uses assert() as its only
-- checking mechanism. Force NDEBUG off so assertions stay live even
-- under -m release (which would otherwise add -DNDEBUG and silently
-- turn every check into a no-op).
local function ts_add_test(name, sources)
    target(name)
        set_kind("binary")
        set_default(false)
        set_group("tests")
        add_files(table.unpack(sources))
        add_includedirs("src")
        add_packages("libuv")
        add_cflags("-UNDEBUG", { force = true })
        if is_plat("linux") then
            add_syslinks("pthread", "dl")
        end
        add_tests("default")
    target_end()
end

ts_add_test("test_smoke",       { "tests/test_smoke.c" })
ts_add_test("test_config",      { "tests/test_config.c",
                                  "src/config.c",
                                  "src/log.c" })
ts_add_test("test_path_utils",  { "tests/test_path_utils.c",
                                  "src/path_utils.c" })
ts_add_test("test_range",       { "tests/test_range.c",
                                  "src/range.c",
                                  "src/log.c" })
ts_add_test("test_http_parser", { "tests/test_http_parser.c",
                                  "src/http_parser.c",
                                  "src/log.c" })
