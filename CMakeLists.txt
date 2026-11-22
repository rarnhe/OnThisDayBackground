cmake_minimum_required(VERSION 3.20)
project(OnThisDayBackground LANGUAGES CXX)

add_library(OnThisDayBackgroundCore STATIC OnThisDayBackground_Core.cpp)
target_include_directories(OnThisDayBackgroundCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(OnThisDayBackgroundCore PUBLIC cxx_std_20)

if(WIN32)
    add_library(OnThisDayBackgroundWindows STATIC
        OnThisDayBackground_Photos.cpp OnThisDayBackground_Desktop.cpp)
    target_link_libraries(OnThisDayBackgroundWindows PUBLIC OnThisDayBackgroundCore
        advapi32 gdi32 ole32 propsys shell32 user32 uuid windowscodecs)
    target_compile_definitions(OnThisDayBackgroundWindows PUBLIC
        UNICODE _UNICODE NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00)

    add_executable(OnThisDayBackground WIN32 OnThisDayBackground.cpp)
    target_link_libraries(OnThisDayBackground PRIVATE OnThisDayBackgroundWindows)
    if(MINGW)
        target_link_options(OnThisDayBackground PRIVATE -municode)
    endif()
endif()

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
