# OnThisDayBackground Windows tests

- Result: **Passed**
- Architecture: x64
- Commit: 104b8d9a98fa639d11a70255456a1de4c2fe5536
- Run: https://github.com/rarnhe/OnThisDayBackground/actions/runs/45199011318

## configure

Outcome: **success**

<pre>-- The CXX compiler identification is MSVC 19.52.37029.0
-- Detecting CXX compiler ABI info
-- Detecting CXX compiler ABI info - done
-- Check for working CXX compiler: C:/Program Files/Microsoft Visual Studio/18/Enterprise/VC/Tools/MSVC/14.52.37025/bin/Hostx64/x64/cl.exe - skipped
-- Detecting CXX compile features
-- Detecting CXX compile features - done
-- Configuring done (8.4s)
-- Generating done (0.0s)
-- Build files have been written to: D:/a/OnThisDayBackground/OnThisDayBackground/out/build/x64-release</pre>

## build

Outcome: **success**

<pre>[1/6] Building CXX object tests\CMakeFiles\test_windows.dir\test_windows.cpp.obj
D:\a\OnThisDayBackground\OnThisDayBackground\tests\test_windows.cpp(93) : warning C6387: &#39;process.hProcess&#39; could be &#39;0&#39;:  this does not adhere to the specification for the function &#39;WaitForSingleObject&#39;. : Lines: 93, 84, 85, 86, 88, 89, 90, 91, 92, 93
D:\a\OnThisDayBackground\OnThisDayBackground\tests\test_windows.cpp(93) : warning C6387: &#39;process.hProcess&#39; could be &#39;0&#39;:  this does not adhere to the specification for the function &#39;GetExitCodeProcess&#39;. See line 93 for an earlier location where this can occur: Lines: 93, 84, 85, 86, 88, 89, 90, 91, 92, 93
D:\a\OnThisDayBackground\OnThisDayBackground\tests\test_windows.cpp(95) : warning C6387: &#39;process.hProcess&#39; could be &#39;0&#39;:  this does not adhere to the specification for the function &#39;TerminateProcess&#39;. See line 93 for an earlier location where this can occur: Lines: 93, 84, 85, 86, 88, 89, 90, 91, 92, 93, 95
D:\a\OnThisDayBackground\OnThisDayBackground\tests\test_windows.cpp(96) : warning C6387: &#39;process.hProcess&#39; could be &#39;0&#39;:  this does not adhere to the specification for the function &#39;CloseHandle&#39;. See line 93 for an earlier location where this can occur: Lines: 93, 84, 85, 86, 88, 89, 90, 91, 92, 93, 95, 96
D:\a\OnThisDayBackground\OnThisDayBackground\tests\test_windows.cpp(145) : warning C6387: &#39;launcher.hProcess&#39; could be &#39;0&#39;:  this does not adhere to the specification for the function &#39;CloseHandle&#39;. : Lines: 143, 107, 105, 108, 111, 112, 113, 114, 115, 116, 117, 118, 119, 121, 123, 122, 121, 123, 122, 121, 125, 126, 127, 128, 129, 130, 133, 134, 135, 136, 137, 138, 140, 141, 142, 143, 144, 145
D:\a\OnThisDayBackground\OnThisDayBackground\tests\test_windows.cpp(183) : warning C6387: &#39;trigger.hProcess&#39; could be &#39;0&#39;:  this does not adhere to the specification for the function &#39;TerminateProcess&#39;. : Lines: 182, 107, 105, 108, 111, 112, 113, 114, 115, 116, 117, 118, 119, 121, 123, 122, 121, 123, 122, 121, 125, 126, 127, 128, 129, 130, 133, 134, 135, 136, 137, 138, 140, 141, 142, 143, 144, 145, 149, 150, 151, 156, 161, 156, 160, 156, 160, 156, 158, 156, 158, 156, 158, 156, 163, 164, 165, 166, 168, 169, 173, 174, 175, 176, 177, 179, 180, 181, 182, 183
D:\a\OnThisDayBackground\OnThisDayBackground\tests\test_windows.cpp(184) : warning C6387: &#39;trigger.hProcess&#39; could be &#39;0&#39;:  this does not adhere to the specification for the function &#39;CloseHandle&#39;. : Lines: 182, 107, 105, 108, 111, 112, 113, 114, 115, 116, 117, 118, 119, 121, 123, 122, 121, 123, 122, 121, 125, 126, 127, 128, 129, 130, 133, 134, 135, 136, 137, 138, 140, 141, 142, 143, 144, 145, 149, 150, 151, 156, 161, 156, 160, 156, 160, 156, 158, 156, 158, 156, 158, 156, 163, 164, 165, 166, 168, 169, 173, 174, 175, 176, 177, 179, 180, 181, 182, 183, 184
[2/6] Building CXX object tests\CMakeFiles\test_on_this_day.dir\test_on_this_day.cpp.obj
[3/6] Building CXX object CMakeFiles\OnThisDayBackgroundSource.dir\OnThisDayBackground.cpp.obj
D:\a\OnThisDayBackground\OnThisDayBackground\OnThisDayBackground.cpp(682) : warning C6031: Return value ignored: &#39;StringFromGUID2&#39;.
D:\a\OnThisDayBackground\OnThisDayBackground\OnThisDayBackground.cpp(901) : warning C6011: Dereferencing NULL pointer &#39;automation&#39;. : Lines: 862, 863, 869, 870, 871, 872, 873, 874, 876, 877, 878, 880, 881, 882, 883, 884, 880, 886, 887, 888, 890, 891, 892, 893, 894, 896, 897, 896, 898, 899, 900, 901
D:\a\OnThisDayBackground\OnThisDayBackground\OnThisDayBackground.cpp(1193) : warning C6387: &#39;attributes&#39; could be &#39;0&#39;:  this does not adhere to the specification for the function &#39;UpdateProcThreadAttribute&#39;. : Lines: 1167, 1168, 1169, 1172, 1173, 1174, 1175, 1178, 1179, 1181, 1180, 1183, 1184, 1185, 1186, 1187, 1191, 1193
[4/6] Linking CXX executable OnThisDayBackground.exe
[5/6] Linking CXX executable tests\test_windows.exe
[6/6] Linking CXX executable tests\test_on_this_day.exe</pre>

## ctest

Outcome: **success**

<pre>Test project D:/a/OnThisDayBackground/OnThisDayBackground/out/build/x64-release
    Start 1: photo_scan_and_rotation
1/8 Test #1: photo_scan_and_rotation ..........   Passed    0.02 sec
    Start 2: windows_date_taken
2/8 Test #2: windows_date_taken ...............   Passed    0.12 sec
    Start 3: windows_desktop_interfaces
3/8 Test #3: windows_desktop_interfaces .......   Passed    0.01 sec
    Start 4: windows_current_desktop_guid
4/8 Test #4: windows_current_desktop_guid .....   Passed    0.01 sec
    Start 5: windows_explorer_refresh_names
5/8 Test #5: windows_explorer_refresh_names ...   Passed    0.01 sec
    Start 6: windows_explorer_refresh_hit
6/8 Test #6: windows_explorer_refresh_hit .....   Passed    0.01 sec
    Start 7: windows_gui_single_instance
7/8 Test #7: windows_gui_single_instance ......   Passed    0.31 sec
    Start 8: windows_removed_list_flag
8/8 Test #8: windows_removed_list_flag ........   Passed    0.03 sec

100% tests passed, 0 tests failed out of 8

Total Test time (real) =   0.54 sec</pre>
