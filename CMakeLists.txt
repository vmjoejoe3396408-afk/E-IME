cmake_minimum_required(VERSION 3.20)
project(EIME VERSION 0.4.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
include(CTest)

if (WIN32)
    add_compile_definitions(NOMINMAX)
endif()
if (MSVC)
    add_compile_options(/utf-8)
endif()

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/include
    ${CMAKE_CURRENT_SOURCE_DIR}/sync/include
    ${CMAKE_CURRENT_SOURCE_DIR}/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/tsf
)

# MinGW 靜態連結標準庫，確保產出的 EXE 與 TSF DLL 100% 獨立無外部 runtime DLL 依賴
if (MINGW)
    add_compile_options($<$<CONFIG:Release>:-O2>)
    add_link_options(-static -static-libgcc -static-libstdc++)
endif()

# 1. 終端互動模擬器與效能基準測試 (eime_cli.exe)
add_executable(eime_cli
    cli/main.cpp
)

# 2. 自動化單元與整合測試套件 (eime_tests.exe)
add_executable(eime_tests
    tests/test_main.cpp
    ui/candidate_window.cpp
)
if (WIN32)
    target_link_libraries(eime_tests PRIVATE user32 gdi32 ole32 uuid)
endif()

# 3. Windows TSF COM 系統輸入法核心元件 (eime_tip.dll)
if (WIN32)
    add_library(eime_tsf SHARED
        tsf/dllmain.cpp
        tsf/text_service.cpp
        tsf/register.cpp
        ui/candidate_window.cpp
    )
    set_target_properties(eime_tsf PROPERTIES
        PREFIX ""
        OUTPUT_NAME "eime_tip"
    )
    if (MINGW)
        # GCC/ld may lose non-ASCII characters in an absolute .def filename.
        configure_file(tsf/eime_tip.def "${CMAKE_CURRENT_BINARY_DIR}/eime_tip.def" COPYONLY)
        target_link_options(eime_tsf PRIVATE eime_tip.def)
        set_property(TARGET eime_tsf APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/eime_tip.def")
        # Some recent MinGW CRTs moved the default fenv to libmingw32. A DLL
        # does not reference _fpreset, so static libmingwex may request this
        # symbol only after that archive has been scanned. Request it early,
        # but only when this toolchain actually provides it in libmingw32.
        execute_process(COMMAND "${CMAKE_CXX_COMPILER}" -print-file-name=libmingw32.a
            OUTPUT_VARIABLE EIME_MINGW32_ARCHIVE OUTPUT_STRIP_TRAILING_WHITESPACE)
        execute_process(COMMAND "${CMAKE_NM}" -g --defined-only "${EIME_MINGW32_ARCHIVE}"
            OUTPUT_VARIABLE EIME_MINGW32_SYMBOLS ERROR_QUIET)
        if (EIME_MINGW32_SYMBOLS MATCHES "[ \t][A-Z][ \t]+__mingw_fe_dfl_env([\r\n]|$)")
            target_link_options(eime_tsf PRIVATE "-Wl,--undefined=__mingw_fe_dfl_env")
        endif()
    else()
        target_sources(eime_tsf PRIVATE tsf/eime_tip.def)
    endif()
    target_link_libraries(eime_tsf PRIVATE
        user32
        gdi32
        ole32
        oleaut32
        uuid
        advapi32
    )

    # 4. 全自動化壓力與混沌測試基準 (eime_testbench.exe)
    add_executable(eime_testbench
        testbench/stress_suite.cpp
        ui/candidate_window.cpp
    )
    target_link_libraries(eime_testbench PRIVATE
        user32
        gdi32
    )

    add_executable(eime_tsf_lifecycle tests/tsf_lifecycle.cpp)
    target_link_libraries(eime_tsf_lifecycle PRIVATE ole32 oleaut32 uuid user32)
    add_executable(eime_tsf_input tests/tsf_input.cpp)
    target_link_libraries(eime_tsf_input PRIVATE ole32 oleaut32 uuid user32)
    if(MINGW)
        target_link_options(eime_tsf_lifecycle PRIVATE -municode)
        target_link_options(eime_tsf_input PRIVATE -municode)
    endif()
endif()

# Every invocation gets fresh data, including repeated/parallel CTest runs.
if (BUILD_TESTING)
    add_executable(eime_sync_tests tests/sync_journal.cpp)
    function(eime_add_test name target)
        set(dll_option "")
        if (WIN32)
            set(dll_option "-DTEST_DLL=$<TARGET_FILE:eime_tsf>")
        endif()
        set(needs_dll_arg OFF)
        if (name MATCHES "^tsf_")
            set(needs_dll_arg ON)
        endif()
        add_test(NAME ${name} COMMAND ${CMAKE_COMMAND}
            "-DTEST_EXE=$<TARGET_FILE:${target}>"
            "-DTEST_NAME=${name}"
            "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/test-runtime"
            "-DDICT=${CMAKE_CURRENT_SOURCE_DIR}/data/core_dict.txt"
            "-DNEEDS_DLL_ARG=${needs_dll_arg}"
            ${dll_option}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_test.cmake")
        set_tests_properties(${name} PROPERTIES TIMEOUT 120)
    endfunction()
    eime_add_test(sync_journal eime_sync_tests)
    eime_add_test(core eime_tests)
    if (WIN32)
        eime_add_test(stress eime_testbench)
        eime_add_test(tsf_lifecycle eime_tsf_lifecycle)
        eime_add_test(tsf_input eime_tsf_input)
    endif()
endif()
