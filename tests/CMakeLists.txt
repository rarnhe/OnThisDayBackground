add_executable(test_on_this_day test_on_this_day.cpp)
target_link_libraries(test_on_this_day PRIVATE OnThisDayBackgroundSource)
add_test(NAME photo_scan_and_rotation COMMAND test_on_this_day)

if(WIN32)
    add_executable(test_windows test_windows.cpp)
    target_link_libraries(test_windows PRIVATE OnThisDayBackgroundSource)
    add_test(NAME windows_date_taken COMMAND test_windows --date-taken
        "${CMAKE_CURRENT_SOURCE_DIR}/filename-2000-08-10.jpg")
    add_test(NAME windows_gui_one_shot COMMAND test_windows --one-shot $<TARGET_FILE:OnThisDayBackground>)
    add_test(NAME windows_cli_photo_list COMMAND test_windows --list $<TARGET_FILE:OnThisDayBackground>
        "${CMAKE_CURRENT_SOURCE_DIR}/filename-2000-08-10.jpg")
    set_tests_properties(windows_gui_one_shot windows_cli_photo_list PROPERTIES TIMEOUT 20)
endif()
