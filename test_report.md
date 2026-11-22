# OnThisDayBackground Windows tests

- Result: **Passed**
- Architecture: x64
- Commit: 071c0f3cb1278f7f3fefcfb33cbc29710c83a615
- Run: https://github.com/rarnhe/OnThisDayBackground/actions/runs/45040038086

## configure

Outcome: **success**

<pre>-- The CXX compiler identification is MSVC 19.52.37029.0
-- Detecting CXX compiler ABI info
-- Detecting CXX compiler ABI info - done
-- Check for working CXX compiler: C:/Program Files/Microsoft Visual Studio/18/Enterprise/VC/Tools/MSVC/14.52.37025/bin/Hostx64/x64/cl.exe - skipped
-- Detecting CXX compile features
-- Detecting CXX compile features - done
-- Configuring done (8.2s)
-- Generating done (0.0s)
-- Build files have been written to: D:/a/OnThisDayBackground/OnThisDayBackground/out/build/x64-release</pre>

## build

Outcome: **success**

<pre>[1/6] Building CXX object tests\CMakeFiles\test_on_this_day.dir\test_on_this_day.cpp.obj
[2/6] Building CXX object tests\CMakeFiles\test_windows.dir\test_windows.cpp.obj
D:\a\OnThisDayBackground\OnThisDayBackground\tests\test_windows.cpp(115) : warning C6387: &#39;process.hProcess&#39; could be &#39;0&#39;:  this does not adhere to the specification for the function &#39;TerminateProcess&#39;. : Lines: 114, 83, 84, 85, 86, 87, 88, 89, 91, 92, 93, 94, 95, 93, 94, 95, 93, 98, 99, 100, 101, 102, 104, 106, 107, 108, 110, 111, 112, 113, 114, 115
D:\a\OnThisDayBackground\OnThisDayBackground\tests\test_windows.cpp(116) : warning C6387: &#39;process.hProcess&#39; could be &#39;0&#39;:  this does not adhere to the specification for the function &#39;CloseHandle&#39;. : Lines: 114, 83, 84, 85, 86, 87, 88, 89, 91, 92, 93, 94, 95, 93, 94, 95, 93, 98, 99, 100, 101, 102, 104, 106, 107, 108, 110, 111, 112, 113, 114, 115, 116
D:\a\OnThisDayBackground\OnThisDayBackground\tests\test_windows.cpp(145) : warning C6387: &#39;launcher.hProcess&#39; could be &#39;0&#39;:  this does not adhere to the specification for the function &#39;CloseHandle&#39;. : Lines: 143, 130, 128, 131, 133, 134, 135, 136, 137, 138, 140, 141, 142, 143, 144, 145
D:\a\OnThisDayBackground\OnThisDayBackground\tests\test_windows.cpp(177) : warning C6387: &#39;trigger.hProcess&#39; could be &#39;0&#39;:  this does not adhere to the specification for the function &#39;TerminateProcess&#39;. : Lines: 176, 130, 128, 131, 133, 134, 135, 136, 137, 138, 140, 141, 142, 143, 144, 145, 149, 150, 151, 156, 160, 161, 162, 163, 165, 166, 168, 171, 172, 176, 177
D:\a\OnThisDayBackground\OnThisDayBackground\tests\test_windows.cpp(178) : warning C6387: &#39;trigger.hProcess&#39; could be &#39;0&#39;:  this does not adhere to the specification for the function &#39;CloseHandle&#39;. : Lines: 176, 130, 128, 131, 133, 134, 135, 136, 137, 138, 140, 141, 142, 143, 144, 145, 149, 150, 151, 156, 160, 161, 162, 163, 165, 166, 168, 171, 172, 176, 177, 178
[3/6] Building CXX object CMakeFiles\OnThisDayBackgroundSource.dir\OnThisDayBackground.cpp.obj
D:\a\OnThisDayBackground\OnThisDayBackground\OnThisDayBackground.cpp(599) : warning C6031: Return value ignored: &#39;StringFromGUID2&#39;.
D:\a\OnThisDayBackground\OnThisDayBackground\OnThisDayBackground.cpp(819) : warning C6011: Dereferencing NULL pointer &#39;automation&#39;. : Lines: 780, 781, 787, 788, 789, 790, 791, 792, 794, 795, 796, 798, 799, 800, 801, 802, 798, 804, 805, 806, 808, 809, 810, 811, 812, 814, 815, 814, 816, 817, 818, 819
D:\a\OnThisDayBackground\OnThisDayBackground\OnThisDayBackground.cpp(1103) : warning C6387: &#39;attributes&#39; could be &#39;0&#39;:  this does not adhere to the specification for the function &#39;UpdateProcThreadAttribute&#39;. : Lines: 1077, 1078, 1079, 1082, 1083, 1084, 1085, 1088, 1089, 1091, 1090, 1093, 1094, 1095, 1096, 1097, 1101, 1103
[4/6] Linking CXX executable tests\test_on_this_day.exe
[5/6] Linking CXX executable tests\test_windows.exe
[6/6] Linking CXX executable OnThisDayBackground.exe</pre>

## ctest

Outcome: **success**

<pre>Test project D:/a/OnThisDayBackground/OnThisDayBackground/out/build/x64-release
    Start 1: photo_scan_and_rotation
1/8 Test #1: photo_scan_and_rotation ..........   Passed    0.01 sec
    Start 2: windows_date_taken
2/8 Test #2: windows_date_taken ...............   Passed    0.08 sec
    Start 3: windows_desktop_interfaces
3/8 Test #3: windows_desktop_interfaces .......   Passed    0.01 sec
    Start 4: windows_current_desktop_guid
4/8 Test #4: windows_current_desktop_guid .....   Passed    0.01 sec
    Start 5: windows_explorer_refresh_names
5/8 Test #5: windows_explorer_refresh_names ...   Passed    0.01 sec
    Start 6: windows_explorer_refresh_hit
6/8 Test #6: windows_explorer_refresh_hit .....   Passed    0.01 sec
    Start 7: windows_gui_single_instance
7/8 Test #7: windows_gui_single_instance ......   Passed    0.13 sec
    Start 8: windows_cli_photo_list
8/8 Test #8: windows_cli_photo_list ...........   Passed    0.08 sec

100% tests passed, 0 tests failed out of 8

Total Test time (real) =   0.35 sec</pre>
