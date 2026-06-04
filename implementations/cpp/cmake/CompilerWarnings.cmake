# osf_set_warnings(target) — apply a sensible warning set per compiler.
#
# Called from each library/executable target's CMakeLists. The flags
# are attached PRIVATE so they do not propagate to downstream consumers
# of the target. Vendored third-party headers are silenced separately
# via target_include_directories(... SYSTEM ...) where they are
# attached.

function(osf_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4
            /permissive-
            /wd4100  # unreferenced formal parameter — common in stub code
        )
        target_compile_definitions(${target} PRIVATE
            _CRT_SECURE_NO_WARNINGS
            NOMINMAX
            WIN32_LEAN_AND_MEAN
        )
    else()
        # GCC / Clang / AppleClang
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wshadow
            -Wconversion
            -Wsign-conversion
        )
    endif()

    # Opt-in warnings-as-errors (default OFF; CI turns it ON). Local dev
    # builds stay lenient; CI enforces a warning-free build on every
    # compiler. Applied per-target so vendored sources with their own
    # per-source warning overrides (e.g. pugixml.cpp) are unaffected.
    if(OSF_WARNINGS_AS_ERRORS)
        if(MSVC)
            target_compile_options(${target} PRIVATE /WX)
        else()
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()
