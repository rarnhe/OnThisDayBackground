add_executable(test_on_this_day test_on_this_day.cpp)
target_link_libraries(test_on_this_day PRIVATE OnThisDayBackgroundSource)
add_test(NAME photo_scan_and_rotation COMMAND test_on_this_day)

if(WIN32)
    add_executable(test_windows test_windows.cpp)
    target_link_libraries(test_windows PRIVATE OnThisDayBackgroundSource)
    add_test(NAME windows_photo_decoder COMMAND test_windows)
    add_test(NAME windows_item_date COMMAND test_windows --item-date
        "${CMAKE_CURRENT_SOURCE_DIR}/filename-2000-08-10.jpg")
    add_test(NAME windows_gui_single_instance COMMAND test_windows --gui $<TARGET_FILE:OnThisDayBackground>)
    set_tests_properties(windows_gui_single_instance PROPERTIES TIMEOUT 20)
endif()
