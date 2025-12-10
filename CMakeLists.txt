cmake_minimum_required(VERSION 3.5)

project(lolpu CXX)

# require C++11 compiler ------------------------------------------------------
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# compile `StreamPU` static lib -----------------------------------------------
option(SPU_TESTS "" OFF) # do NOT compile the tests
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/lib/streampu/)

# create the executables ------------------------------------------------------
add_executable(lolpu ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp)
add_executable(test_udp_chain ${CMAKE_CURRENT_SOURCE_DIR}/src/test_udp_chain.cpp)
add_executable(tx_main ${CMAKE_CURRENT_SOURCE_DIR}/src/tx_main.cpp)
add_executable(rx_main ${CMAKE_CURRENT_SOURCE_DIR}/src/rx_main.cpp)
add_executable(rx_continuous ${CMAKE_CURRENT_SOURCE_DIR}/src/rx_continuous.cpp)
add_executable(tx_continuous ${CMAKE_CURRENT_SOURCE_DIR}/src/tx_continuous.cpp)
add_executable(UdpReassembler_test ${CMAKE_CURRENT_SOURCE_DIR}/src/UdpReassembler_test.cpp)

# add include directories -----------------------------------------------------
target_include_directories(lolpu PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(test_udp_chain PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(tx_main PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(rx_main PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(rx_continuous PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(tx_continuous PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(UdpReassembler_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# link with `StreamPU` static lib --------------------------------------------
target_link_libraries(lolpu PUBLIC spu-static-lib)
target_link_libraries(test_udp_chain PUBLIC spu-static-lib)
target_link_libraries(tx_main PUBLIC spu-static-lib)
target_link_libraries(rx_main PUBLIC spu-static-lib)
target_link_libraries(rx_continuous PUBLIC spu-static-lib)
target_link_libraries(tx_continuous PUBLIC spu-static-lib)
target_link_libraries(UdpReassembler_test PUBLIC spu-static-lib)

# find and link with thread library ------------------------------------------
set(CMAKE_THREAD_PREFER_PTHREAD ON)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(lolpu PUBLIC Threads::Threads)
target_link_libraries(UdpReassembler_test PUBLIC Threads::Threads)

# link with `cpptrace` (`SPU_STACKTRACE=ON`) ---------------------------------
if(SPU_STACKTRACE OR SPU_STACKTRACE_SEGFAULT)
  target_link_libraries(lolpu PUBLIC cpptrace::cpptrace)
  target_link_libraries(UdpReassembler_test PUBLIC cpptrace::cpptrace)
endif()