cmake_minimum_required(VERSION 3.16)
project(calliope LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED true)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include(FetchContent)


# set the output directory for built objects.
# This makes sure that the dynamic library goes into the build directory automatically.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")


add_executable(calliope 
    main.cpp 
    # AudioProcessor.cpp CommandProcessor.cpp
    # grammar-parser.cpp common.cpp 
    # ${WHISPER_DIR}/src/whisper.cpp 
)

set_target_properties(calliope PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/build"
)
# target_include_directories(calliope PRIVATE ${WHISPER_DIR}/include)
# target_link_libraries(calliope PRIVATE whisper ${CMAKE_THREAD_LIBS_INIT})
# target_link_libraries(calliope PRIVATE SDL3::SDL3)
install(TARGETS calliope RUNTIME)
