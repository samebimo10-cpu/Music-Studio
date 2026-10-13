include(FetchContent)

# ---- JUCE -------------------------------------------------------------------
if(STUDIO_JUCE_DIR)
    add_subdirectory(${STUDIO_JUCE_DIR} ${CMAKE_BINARY_DIR}/juce EXCLUDE_FROM_ALL)
else()
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.9
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(juce)
endif()

# ---- SQLite (library index) ---------------------------------------------------
find_package(SQLite3 QUIET)
if(NOT SQLite3_FOUND)
    message(STATUS "System SQLite not found, building the amalgamation")
    FetchContent_Declare(sqlite_amalgamation
        GIT_REPOSITORY https://github.com/azadkuh/sqlite-amalgamation.git
        GIT_TAG        master
        GIT_SHALLOW    TRUE)
    FetchContent_GetProperties(sqlite_amalgamation)
    if(NOT sqlite_amalgamation_POPULATED)
        FetchContent_Populate(sqlite_amalgamation)
    endif()
    add_library(studio_sqlite STATIC ${sqlite_amalgamation_SOURCE_DIR}/sqlite3.c)
    target_include_directories(studio_sqlite PUBLIC ${sqlite_amalgamation_SOURCE_DIR})
    set_target_properties(studio_sqlite PROPERTIES POSITION_INDEPENDENT_CODE ON)
    add_library(SQLite::SQLite3 ALIAS studio_sqlite)
endif()
