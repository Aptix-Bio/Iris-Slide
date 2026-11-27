# RTM Encode_09
# CommandLine_24.13: one command-line case, run as `cmake -P`: IrisSlideEncoder (EXE) with the
# comma-separated ARGUMENTS must exit with CODE and print text matching EXPECT, on stdout or
# stderr. ctest's PASS_REGULAR_EXPRESSION alone ignores the exit status, so both are checked here.
string(REPLACE "," ";" arguments "${ARGUMENTS}")
execute_process(COMMAND "${EXE}" ${arguments}
                RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
if (NOT code STREQUAL CODE)
    message(FATAL_ERROR "IrisSlideEncoder ${ARGUMENTS} exited ${code} where ${CODE} is expected:\n${out}${err}")
endif()
if (NOT "${out}${err}" MATCHES "${EXPECT}")
    message(FATAL_ERROR "IrisSlideEncoder ${ARGUMENTS} printed no match for \"${EXPECT}\":\n${out}${err}")
endif()
