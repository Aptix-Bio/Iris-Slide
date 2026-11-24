# VcpkgCaching_2 (OneReusableBuild_2.5; RTM Build_06): what a CI job's caches read and write, in two
# calls around the sign-in (.github/actions/caches).
#
# STEP=mode, before the sign-in: the access the job gets, written to GITHUB_OUTPUT as `mode`.
#   - A pull request from a fork gets `none`: GitHub gives it no OIDC token, so it cannot sign in,
#     and it builds with an empty vcpkg folder and no compiler cache. A public bucket was the
#     alternative; it was refused because anyone could then download from it at Aptix's expense.
#   - Every other run gets what its workflow asked for: `quarantine` (pull requests and checks:
#     read main's store, write the pull requests' quarantine), `read` (releases: read main's
#     store, write nothing), `write` (cache.yml on main only; Google refuses any other run).
#   It fails, naming them, when the repository variables the sign-in needs are unset.
# STEP=env, after the sign-in: the variables vcpkg and sccache read, appended to GITHUB_ENV.
#
# The store: gs://iris-vcpkg-cache, objects under iris-slide/ only (tools/cloud_federation.sh).
#
# Inputs (environment): MODE and COMPILER_CACHE (STEP=env), REQUESTED, EVENT, HEAD_REPOSITORY, REPOSITORY,
# GCP_PROVIDER, GCP_PROJECT, GCS_TOKEN (STEP=env), RUNNER_TEMP, GITHUB_OUTPUT, GITHUB_ENV.
# Usage: cmake -DSTEP=mode|env -P scripts/ci_caches.cmake
cmake_minimum_required(VERSION 3.27)
set(bucket iris-vcpkg-cache)
set(prefix iris-slide)
set(variables_help "set them from tools/cloud_federation.sh's output: gh variable set IRIS_SLIDE_GCP_PROVIDER --body <provider>; gh variable set IRIS_SLIDE_GCP_PROJECT --body <project>")

function(ci_caches_fail text)
    message(FATAL_ERROR "ci_caches: ${text}")
endfunction()

if(STEP STREQUAL "mode")
    set(requested "$ENV{REQUESTED}")
    if(NOT requested MATCHES "^(quarantine|read|write)$")
        ci_caches_fail("the workflow asked for access '${requested}'; expected quarantine, read or write")
    endif()
    set(mode "${requested}")
    if("$ENV{EVENT}" MATCHES "^pull_request" AND NOT "$ENV{HEAD_REPOSITORY}" STREQUAL "$ENV{REPOSITORY}")
        set(mode none)
        message(NOTICE "A pull request from $ENV{HEAD_REPOSITORY}: GitHub gives it no sign-in, so it builds without the cache store")
    endif()
    if(NOT mode STREQUAL "none")
        set(missing "")
        foreach(variable GCP_PROVIDER GCP_PROJECT)
            if("$ENV{${variable}}" STREQUAL "")
                list(APPEND missing "IRIS_SLIDE_${variable}")
            endif()
        endforeach()
        if(missing)
            list(JOIN missing ", " missing)
            ci_caches_fail("the repository variables ${missing} are unset, so the job cannot sign in to the cache store; ${variables_help}")
        endif()
    endif()
    file(APPEND "$ENV{GITHUB_OUTPUT}" "mode=${mode}\n")
    message(STATUS "cache access: ${mode}")
elseif(STEP STREQUAL "env")
    set(mode "$ENV{MODE}")
    set(archives "$ENV{RUNNER_TEMP}/vcpkg-archives")
    file(MAKE_DIRECTORY "${archives}")
    set(local "files,${archives},readwrite")
    if(mode STREQUAL "none")
        file(APPEND "$ENV{GITHUB_ENV}" "VCPKG_BINARY_SOURCES=clear;${local}\n")
        return()
    endif()
    if("$ENV{GCS_TOKEN}" STREQUAL "")
        ci_caches_fail("the sign-in returned no access token")
    endif()
    set(auth "Authorization: Bearer $ENV{GCS_TOKEN}")
    set(object "{triplet}/{name}-{version}-{sha}.zip")
    set(main "http,https://storage.googleapis.com/${bucket}/${prefix}/vcpkg/${object}")
    set(quarantine "http,https://storage.googleapis.com/${bucket}/${prefix}/pr/vcpkg/${object}")
    if(mode STREQUAL "quarantine")
        set(vcpkg "${main},read,${auth};${quarantine},readwrite,${auth}")
        set(sccache_prefix "${prefix}/pr/sccache")
        set(sccache_mode READ_WRITE)
    elseif(mode STREQUAL "read")
        set(vcpkg "${main},read,${auth}")
        set(sccache_prefix "${prefix}/sccache")
        set(sccache_mode READ_ONLY)
    elseif(mode STREQUAL "write")
        set(vcpkg "${main},readwrite,${auth}")
        set(sccache_prefix "${prefix}/sccache")
        set(sccache_mode READ_WRITE)
    else()
        ci_caches_fail("mode is '${mode}'; expected none, read, quarantine or write")
    endif()
    file(APPEND "$ENV{GITHUB_ENV}" "VCPKG_BINARY_SOURCES=clear;${vcpkg};${local}\n")
    if(NOT "$ENV{COMPILER_CACHE}" STREQUAL "true")
        return()
    endif()
    # sccache is kept up for the whole job (no idle timeout), so the report sees every compile;
    # CMake reads the launchers from the environment, so no preset names sccache.
    file(APPEND "$ENV{GITHUB_ENV}"
        "SCCACHE_GCS_BUCKET=${bucket}\n"
        "SCCACHE_GCS_KEY_PREFIX=${sccache_prefix}\n"
        "SCCACHE_GCS_RW_MODE=${sccache_mode}\n"
        "SCCACHE_IDLE_TIMEOUT=0\n"
        "CMAKE_C_COMPILER_LAUNCHER=sccache\n"
        "CMAKE_CXX_COMPILER_LAUNCHER=sccache\n")
else()
    ci_caches_fail("STEP is '${STEP}'; expected mode or env")
endif()
