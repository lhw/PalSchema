include(FetchContent)

if(WIN32)
    add_subdirectory("RE-UE4SS")
else()
    add_subdirectory("ue4ss-linux")
endif()

add_subdirectory("json")
add_subdirectory("glaze")
add_subdirectory("efsw")

if(WIN32)
    add_subdirectory("safetyhook")
endif()
# On Linux, UE4SS provides funchook via its own deps (linked transitively)