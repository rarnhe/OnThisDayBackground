cmake_minimum_required(VERSION 3.20)
project(OnThisDayBackground LANGUAGES CXX)

add_library(OnThisDayBackgroundSource OBJECT OnThisDayBackground.cpp OnThisDayBackground.h)
target_include_directories(OnThisDayBackgroundSource PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(OnThisDayBackgroundSource PUBLIC cxx_std_20)

if(WIN32)
    target_link_libraries(OnThisDayBackgroundSource PUBLIC
        advapi32 ole32 oleaut32 propsys runtimeobject shell32 uiautomationcore user32 uuid)
    target_compile_definitions(OnThisDayBackgroundSource PUBLIC
        UNICODE _UNICODE NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00)

    add_executable(OnThisDayBackground WIN32)
    target_link_libraries(OnThisDayBackground PRIVATE OnThisDayBackgroundSource)
    if(MINGW)
        target_link_options(OnThisDayBackground PRIVATE -municode)
    endif()
endif()

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
