function(magdrum_apply_juce_patches juce_source_dir)
    find_package(Git REQUIRED)

    set(patch_file "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/patches/juce-vst3-waveshell-host-context.patch")
    if(NOT EXISTS "${patch_file}")
        message(FATAL_ERROR "Required JUCE compatibility patch is missing: ${patch_file}")
    endif()

    execute_process(
        COMMAND "${GIT_EXECUTABLE}" apply --check "${patch_file}"
        WORKING_DIRECTORY "${juce_source_dir}"
        RESULT_VARIABLE patch_check_result
        OUTPUT_QUIET
        ERROR_QUIET)

    if(patch_check_result EQUAL 0)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" apply "${patch_file}"
            WORKING_DIRECTORY "${juce_source_dir}"
            RESULT_VARIABLE patch_result
            ERROR_VARIABLE patch_error)
        if(NOT patch_result EQUAL 0)
            message(FATAL_ERROR "Could not apply JUCE WaveShell compatibility patch: ${patch_error}")
        endif()
        message(STATUS "Applied JUCE WaveShell host-context compatibility patch")
        return()
    endif()

    execute_process(
        COMMAND "${GIT_EXECUTABLE}" apply --reverse --check "${patch_file}"
        WORKING_DIRECTORY "${juce_source_dir}"
        RESULT_VARIABLE reverse_check_result
        OUTPUT_QUIET
        ERROR_QUIET)

    if(NOT reverse_check_result EQUAL 0)
        message(FATAL_ERROR "The JUCE source does not match the expected state for the WaveShell host-context patch")
    endif()

    message(STATUS "JUCE WaveShell host-context compatibility patch is already applied")
endfunction()
