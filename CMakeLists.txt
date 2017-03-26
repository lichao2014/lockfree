cmake_minimum_required(VERSION 2.8)

cmake_policy(SET CMP0048 NEW)
project(lockfree VERSION 1.0.0)

aux_source_directory(. SRC)
file(GLOB INC *.h)

if (NOT WIN32)
	link_libraries(pthread)
endif()

add_executable(lockfree_test ${SRC} ${INC})
