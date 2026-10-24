cmake_minimum_required(VERSION 3.20)
project(EIME VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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
