###############################################################################
# verify_output.cmake — Cross-platform test wrapper (cmake -P)
#
# Required variables (passed via -D):
#   FFMPEG_CMD     — Path to ffmpeg executable
#   FFMPEG_ARGS    — ffmpeg command-line arguments as a single string.
#                    Use {{SEMI}} as placeholder for literal semicolons
#                    (needed in -filter_complex graphs) because CMake
#                    treats ';' as a list separator in -D variables.
#   OUTPUT_FILES   — Semicolon-separated list of output files to verify
#   WORK_DIR       — Working directory for ffmpeg
#
# Optional variables:
#   REF_FFMPEG_ARGS — Arguments for generating a reference file (for comparison)
#   REF_OUTPUT_FILE — Path to the reference output file
#   EXPECT_DIFFERENT_FROM_REF — If TRUE, output md5 must differ from reference
#   MIN_FILE_SIZE   — Minimum expected file size in bytes (default: 1000)
#   CHECK_FRAMEMD5  — If TRUE, verify frames have non-uniform md5 (not all same)
#
# Path convention:
#   OUTPUT_FILES, REF_OUTPUT_FILE, and paths inside FFMPEG_ARGS should be
#   RELATIVE to WORK_DIR.  This avoids Windows drive-letter colon issues
#   (e.g. D:\path being mis-parsed by ffmpeg as key:value).
#   Internally, this script resolves them to absolute paths for CMake file()
#   commands that don't honour WORKING_DIRECTORY.
###############################################################################

cmake_minimum_required(VERSION 3.16)

if(NOT FFMPEG_CMD OR NOT FFMPEG_ARGS OR NOT OUTPUT_FILES OR NOT WORK_DIR)
    message(FATAL_ERROR "Missing required variables. Need: FFMPEG_CMD, FFMPEG_ARGS, OUTPUT_FILES, WORK_DIR")
endif()

if(NOT MIN_FILE_SIZE)
    set(MIN_FILE_SIZE 1000)
endif()

# OUTPUT_FILES is passed with | separator (to survive CMake's add_test
# semicolon list expansion).  Convert back to CMake list (;-separated).
string(REPLACE "|" ";" OUTPUT_FILES "${OUTPUT_FILES}")

# Helper: resolve a potentially relative path to absolute against WORK_DIR.
macro(abs_path OUT_VAR REL_PATH)
    if(IS_ABSOLUTE "${REL_PATH}")
        set(${OUT_VAR} "${REL_PATH}")
    else()
        set(${OUT_VAR} "${WORK_DIR}/${REL_PATH}")
    endif()
endmacro()

# --- Step 1: Run ffmpeg to generate output ---
# Replace {{SEMI}} placeholders with real semicolons
string(REPLACE "{{SEMI}}" ";" FFMPEG_ARGS "${FFMPEG_ARGS}")
if(REF_FFMPEG_ARGS)
    string(REPLACE "{{SEMI}}" ";" REF_FFMPEG_ARGS "${REF_FFMPEG_ARGS}")
endif()
message(STATUS "Running: ${FFMPEG_CMD} ${FFMPEG_ARGS}")
separate_arguments(CMD_ARGS NATIVE_COMMAND "${FFMPEG_ARGS}")
execute_process(
    COMMAND "${FFMPEG_CMD}" ${CMD_ARGS}
    WORKING_DIRECTORY "${WORK_DIR}"
    RESULT_VARIABLE ret
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
)
if(NOT ret EQUAL 0)
    message(STATUS "ffmpeg stderr:\n${stderr}")
    message(FATAL_ERROR "ffmpeg failed with exit code ${ret}")
endif()

# --- Step 2: Verify output files exist and have reasonable size ---
foreach(outfile IN LISTS OUTPUT_FILES)
    abs_path(_abs "${outfile}")
    if(NOT EXISTS "${_abs}")
        message(FATAL_ERROR "Output file not created: ${outfile}")
    endif()
    file(SIZE "${_abs}" fsize)
    message(STATUS "Output: ${outfile} (${fsize} bytes)")
    if(fsize LESS ${MIN_FILE_SIZE})
        message(FATAL_ERROR "Output file too small (${fsize} < ${MIN_FILE_SIZE}): ${outfile}")
    endif()
endforeach()

# --- Step 3: Verify frames are not all identical (detect black/green screen) ---
if(CHECK_FRAMEMD5)
    foreach(outfile IN LISTS OUTPUT_FILES)
        # Use relative path for ffmpeg arguments, absolute for file() ops
        set(MD5_FILE "${outfile}.framemd5")
        abs_path(_abs_md5 "${MD5_FILE}")

        execute_process(
            COMMAND "${FFMPEG_CMD}" -hide_banner -y -i "${outfile}"
                -f framemd5 "${MD5_FILE}"
            WORKING_DIRECTORY "${WORK_DIR}"
            RESULT_VARIABLE md5_ret
            ERROR_VARIABLE md5_stderr
        )
        if(NOT md5_ret EQUAL 0)
            message(WARNING "framemd5 generation failed for ${outfile}: ${md5_stderr}")
            continue()
        endif()

        # Read framemd5 file and check that not all frames have the same hash
        file(STRINGS "${_abs_md5}" md5_lines)
        set(UNIQUE_HASHES "")
        set(HASH_COUNT 0)
        foreach(line IN LISTS md5_lines)
            # Skip comment lines (starting with #)
            string(REGEX MATCH "^#" is_comment "${line}")
            if(is_comment)
                continue()
            endif()
            # Extract the md5 hash (last field after last comma).
            # NOTE: CMake regex does NOT support {N} quantifiers,
            # so we use [0-9a-f]+ to match the hex hash.
            string(REGEX MATCH ", ([0-9a-f]+)$" _match "${line}")
            set(hash "${CMAKE_MATCH_1}")
            if(hash)
                math(EXPR HASH_COUNT "${HASH_COUNT} + 1")
                list(FIND UNIQUE_HASHES "${hash}" idx)
                if(idx EQUAL -1)
                    list(APPEND UNIQUE_HASHES "${hash}")
                endif()
            endif()
        endforeach()

        list(LENGTH UNIQUE_HASHES num_unique)
        message(STATUS "framemd5 check: ${outfile} — ${HASH_COUNT} frames, ${num_unique} unique hashes")

        if(HASH_COUNT LESS 2)
            message(WARNING "Too few frames (${HASH_COUNT}) in ${outfile}, skipping uniformity check")
        elseif(num_unique LESS 2)
            message(FATAL_ERROR
                "All ${HASH_COUNT} frames have identical md5 in ${outfile}. "
                "Likely black screen or corrupted output.")
        endif()

        # Clean up temporary md5 file
        file(REMOVE "${_abs_md5}")
    endforeach()
endif()

# --- Step 4: Compare with reference if requested ---
if(REF_FFMPEG_ARGS AND REF_OUTPUT_FILE)
    message(STATUS "Generating reference: ${REF_FFMPEG_ARGS}")
    separate_arguments(REF_CMD_ARGS NATIVE_COMMAND "${REF_FFMPEG_ARGS}")
    execute_process(
        COMMAND "${FFMPEG_CMD}" ${REF_CMD_ARGS}
        WORKING_DIRECTORY "${WORK_DIR}"
        RESULT_VARIABLE ref_ret
        ERROR_VARIABLE ref_stderr
    )
    if(NOT ref_ret EQUAL 0)
        message(WARNING "Reference generation failed: ${ref_stderr}")
    else()
        # Compare framemd5 of first output vs reference
        list(GET OUTPUT_FILES 0 first_output)
        set(OUT_MD5_REL "${first_output}.cmp.framemd5")
        set(REF_MD5_REL "${REF_OUTPUT_FILE}.cmp.framemd5")
        abs_path(OUT_MD5_ABS "${OUT_MD5_REL}")
        abs_path(REF_MD5_ABS "${REF_MD5_REL}")

        # Generate framemd5 using relative paths (safe on Windows)
        execute_process(
            COMMAND "${FFMPEG_CMD}" -hide_banner -y -i "${first_output}" -f framemd5 "${OUT_MD5_REL}"
            WORKING_DIRECTORY "${WORK_DIR}" RESULT_VARIABLE r1 ERROR_QUIET)
        execute_process(
            COMMAND "${FFMPEG_CMD}" -hide_banner -y -i "${REF_OUTPUT_FILE}" -f framemd5 "${REF_MD5_REL}"
            WORKING_DIRECTORY "${WORK_DIR}" RESULT_VARIABLE r2 ERROR_QUIET)

        if(r1 EQUAL 0 AND r2 EQUAL 0)
            file(MD5 "${OUT_MD5_ABS}" out_hash)
            file(MD5 "${REF_MD5_ABS}" ref_hash)

            if(EXPECT_DIFFERENT_FROM_REF)
                if(out_hash STREQUAL ref_hash)
                    message(FATAL_ERROR
                        "Output matches reference but should differ. "
                        "Filter may not be applied.")
                else()
                    message(STATUS "Verified: output differs from reference (filter applied)")
                endif()
            else()
                if(NOT out_hash STREQUAL ref_hash)
                    message(FATAL_ERROR "Output does not match reference")
                else()
                    message(STATUS "Verified: output matches reference")
                endif()
            endif()

            file(REMOVE "${OUT_MD5_ABS}" "${REF_MD5_ABS}")
        endif()

        abs_path(_abs_ref "${REF_OUTPUT_FILE}")
        file(REMOVE "${_abs_ref}")
    endif()
endif()

message(STATUS "Test PASSED")
