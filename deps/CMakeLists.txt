include(FetchContent)

if(WIN32)
    add_subdirectory("RE-UE4SS")
else()
    # Disable UE4SS GUI (GLFW requires X11) — PalSchema doesn't use it
    set(UE4SS_GUI_ENABLED OFF CACHE BOOL "" FORCE)
    add_subdirectory("ue4ss-linux")
endif()

add_subdirectory("json")
add_subdirectory("glaze")
add_subdirectory("efsw")

if(WIN32)
    add_subdirectory("safetyhook")
endif()
# On Linux, UE4SS provides funchook via its own deps (linked transitively)