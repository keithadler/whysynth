# Apply a patch to a fetched dependency's source tree, once.
#   cmake -DPATCH=<file> -P ApplyPatch.cmake      (run by FetchContent's PATCH_COMMAND, in the source dir)
# A reverse dry run passing means the patch is already in, so re-running configure is harmless.
execute_process(COMMAND git apply --check -R "${PATCH}" RESULT_VARIABLE already OUTPUT_QUIET ERROR_QUIET)
if(NOT already EQUAL 0)
    execute_process(COMMAND git apply --ignore-whitespace "${PATCH}" RESULT_VARIABLE rc)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "could not apply ${PATCH}")
    endif()
    message(STATUS "applied ${PATCH}")
endif()
