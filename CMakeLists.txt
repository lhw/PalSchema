cmake_minimum_required(VERSION 3.22)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_subdirectory(deps)

set(TARGET PalSchema)
project(${TARGET})

file(GLOB SRC_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/dllmain.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Generator/JsonSchema/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Loader/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Loader/Blueprint/PalBlueprintMod.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Loader/Spawner/PalListInfo.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Loader/Spawner/PalSpawnGroupListInfo.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Loader/Spawner/SpawnerInfo.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Loader/WildcardFilter/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/SDK/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/SDK/Classes/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/SDK/Classes/Custom/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/SDK/Classes/Custom/DataTable/TableSerializer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/SDK/Classes/Property/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/SDK/Helper/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/SDK/Structs/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/SDK/Structs/Reflected/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/SDK/Structs/Custom/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/SDK/Structs/Custom/DataTable/FTableSerializerRow.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Tools/EnumSchemaDefinitionGenerator.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Utility/Config.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Utility/JsonHelpers.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Utility/EnumHelpers.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Misc/FileWatchWrapper.cpp"
)

add_library(${TARGET} SHARED ${SRC_FILES})

target_include_directories(${TARGET} PRIVATE
    include/
)

# UE4SS target provides all first-party libs, Zydis, Zycore, funchook/safetyhook transitively
# On Windows: Zydis, Zycore, safetyhook come from RE-UE4SS deps
# On Linux: Zydis, funchook come from ue4ss-linux deps
target_link_libraries(${TARGET} PRIVATE
    nlohmann_json::nlohmann_json
    glaze::glaze
    efsw
    UE4SS
)

if(WIN32)
    # Windows: no extra system libs needed beyond what UE4SS provides
else()
    # Linux: dlopen/dlsym for runtime symbol resolution
    target_link_libraries(${TARGET} PRIVATE dl pthread)
    # FORCE_U16 is already a PUBLIC define from UE4SS Linux, but ensure it's set
    target_compile_definitions(${TARGET} PRIVATE FORCE_U16 PLATFORM_LINUX)
    # Suppress GCC warnings from UE4SS headers
    target_compile_options(${TARGET} PRIVATE -Wno-changes-meaning -Wno-template-body)
endif()

# Symbol visibility: ensure all exports are visible for UE4SS to dlopen
set_target_properties(${TARGET} PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    C_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)
