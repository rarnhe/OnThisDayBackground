# Resolve the LLVM executables before project() enables C++.
function(on_this_day_find_llvm output executable)
    if(IS_ABSOLUTE "${${output}}" AND EXISTS "${${output}}")
        get_filename_component(current_name "${${output}}" NAME)
        string(TOLOWER "${current_name}" current_name)
        if(current_name STREQUAL executable)
            return()
        endif()
    endif()

    unset(llvm_program CACHE)
    find_program(llvm_program NAMES "${executable}" HINTS ${llvm_paths})
    if(NOT llvm_program)
        message(FATAL_ERROR
            "Could not find ${executable}. In Visual Studio Installer, install 'C++ Clang tools for Windows', "
            "or set LLVM_ROOT to the root of a standalone LLVM installation. "
            "Then delete the x64-release CMake cache and configure again.")
    endif()
    set("${output}" "${llvm_program}" CACHE FILEPATH "LLVM ${executable}" FORCE)
    unset(llvm_program CACHE)
endfunction()

set(llvm_roots "${LLVM_ROOT}" "$ENV{LLVM_ROOT}" "$ENV{LLVMInstallDir}")
if(NOT "$ENV{VCINSTALLDIR}" STREQUAL "")
    list(APPEND llvm_roots "$ENV{VCINSTALLDIR}/Tools/Llvm")
endif()
if(NOT "$ENV{VSINSTALLDIR}" STREQUAL "")
    list(APPEND llvm_roots "$ENV{VSINSTALLDIR}/VC/Tools/Llvm")
endif()

set(vswhere "$ENV{ProgramFiles\(x86\)}/Microsoft Visual Studio/Installer/vswhere.exe")
if(EXISTS "${vswhere}")
    execute_process(
        COMMAND "${vswhere}" -all -products "*"
            -requires Microsoft.VisualStudio.Component.VC.Llvm.Clang -property installationPath
        OUTPUT_VARIABLE vs_installations
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    string(REPLACE "\r" "" vs_installations "${vs_installations}")
    string(REPLACE "\n" ";" vs_installations "${vs_installations}")
    foreach(installation IN LISTS vs_installations)
        if(installation)
            list(APPEND llvm_roots "${installation}/VC/Tools/Llvm")
        endif()
    endforeach()
endif()

foreach(program_files "$ENV{ProgramW6432}" "$ENV{ProgramFiles}")
    if(program_files)
        list(APPEND llvm_roots "${program_files}/LLVM")
    endif()
endforeach()

set(llvm_paths)
foreach(root IN LISTS llvm_roots)
    if(root)
        list(APPEND llvm_paths "${root}/x64/bin" "${root}/bin" "${root}/arm64/bin")
    endif()
endforeach()

on_this_day_find_llvm(CMAKE_CXX_COMPILER clang-cl.exe)
on_this_day_find_llvm(CMAKE_LINKER lld-link.exe)
