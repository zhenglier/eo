file(GLOB SRCS "*.cpp")
add_library(solution_lib STATIC ${SRCS})

target_include_directories(solution_lib
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}
)

target_compile_features(solution_lib PUBLIC cxx_std_14)