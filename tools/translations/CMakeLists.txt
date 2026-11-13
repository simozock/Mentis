# FEAT-091: the translations tool (JSON <-> .ts, offered languages), run at build time. Part of the
# main build, or built alone with a desktop Qt (FEAT-097): an Android build cannot run its own
# executables, so it takes this one from the host (MENTIS_HOST_TRANSLATIONS in the top CMakeLists.txt).
cmake_minimum_required(VERSION 3.24)
project(MentisTranslations LANGUAGES CXX)

if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
    if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_BUILD_TYPE Release)
    endif()
    find_package(Qt6 6.8 REQUIRED COMPONENTS Core)
endif()

qt_add_executable(MentisTranslations
    main.cpp
    translationfiles.h
    translationfiles.cpp
)
target_link_libraries(MentisTranslations PRIVATE Qt6::Core)
# A folder of its own: on Windows the build runs the tool with a copy of Qt Core beside it, whatever
# the PATH. In the build folder that copy made the app and the tests load Qt from there and miss
# its plugins ("no Qt platform plugin could be initialized", 2026-11-02).
set_target_properties(MentisTranslations PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools/translations")
if(WIN32)
    add_custom_command(TARGET MentisTranslations POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:Qt6::Core> $<TARGET_FILE_DIR:MentisTranslations>)
endif()
