# CMakeList.txt : CMake project for FlowAggregationService, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.8)

# Add source to this project's executable.

add_executable (FlowAggregationService "FlowAggregationService.cpp" "FlowAggregationService.h" "FlowRecord.cpp" "FlowRecord.h" "FlowCollector.cpp" "FlowCollector.h" "FlowAggregator.cpp" "FlowAggregator.h" "InternalError.h")

set(BOOST_PATH "$ENV{HOME}/FlowAggregationService/out/boost")
include_directories("${BOOST_PATH}/include/") 
file(GLOB BOOST_LIBS "${BOOST_PATH}/lib/libboost*.a")

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

target_link_libraries( FlowAggregationService ${BOOST_LIBS} Threads::Threads ) 

# TODO: Add tests and install targets if needed.
