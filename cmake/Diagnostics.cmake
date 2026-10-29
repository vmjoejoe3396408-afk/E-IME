# Optional, pinned upstream dependency; ordinary builds remain offline.
set(EIME_TRACELOGGING_REVISION daaff5ffd5e3613f2c1a91f323350e98e93e80b3)
set(trace_cache "${CMAKE_BINARY_DIR}/_deps/microsoft-tracelogging")
file(MAKE_DIRECTORY "${trace_cache}")
function(eime_trace_dependency name relative hash)
    if (EXISTS "${trace_cache}/${name}")
        file(SHA256 "${trace_cache}/${name}" actual)
        if (NOT actual STREQUAL hash)
            message(FATAL_ERROR "Unexpected TraceLogging dependency hash: ${name}")
        endif()
    else()
        file(DOWNLOAD
            "https://raw.githubusercontent.com/microsoft/tracelogging/${EIME_TRACELOGGING_REVISION}/${relative}"
            "${trace_cache}/${name}" EXPECTED_HASH "SHA256=${hash}" TLS_VERIFY ON)
    endif()
endfunction()
eime_trace_dependency(TraceLoggingDynamic.h etw/cpp/traceloggingdynamic/TraceLoggingDynamic.h
    6eb2a1e41cd335c8c00382284203ec81aea4fb58874cec6be2911c67c6f4c37c)
eime_trace_dependency(LICENSE LICENSE
    7dc541691bf807a74bf7c1090662712af131571f9be8fb8a6f6c1e6dbfbe2fbb)
execute_process(COMMAND git describe --always --dirty
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" OUTPUT_VARIABLE trace_revision
    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if (NOT trace_revision)
    set(trace_revision unknown)
endif()
target_sources(eime_tsf PRIVATE tsf/diagnostics.cpp)
target_include_directories(eime_tsf PRIVATE "${trace_cache}")
target_compile_definitions(eime_tsf PRIVATE EIME_ENABLE_DIAGNOSTICS
    EIME_BUILD_REVISION="${trace_revision}"
    EIME_BUILD_COMPILER="${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}")
