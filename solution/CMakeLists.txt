enable_language(CXX)

set(SRCS
    Duration.cpp
        GAInit.cpp
    solution.cpp
)

if(NOT SRCS)
    message(FATAL_ERROR "No sources found for solution_lib in ${CMAKE_CURRENT_SOURCE_DIR}")
endif()

add_library(solution_lib STATIC ${SRCS})
set_target_properties(solution_lib PROPERTIES LINKER_LANGUAGE CXX)

target_include_directories(solution_lib
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}
)

target_compile_features(solution_lib PUBLIC cxx_std_14)