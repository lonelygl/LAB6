cmake_minimum_required(VERSION 3.14)
project(ufc_db CXX)

set(CMAKE_CXX_STANDARD 17)

# Find libpq (PostgreSQL C library)
find_package(PostgreSQL REQUIRED)

add_executable(ufc_db main.cpp)
target_include_directories(ufc_db PRIVATE ${PostgreSQL_INCLUDE_DIRS})
target_link_libraries(ufc_db PRIVATE ${PostgreSQL_LIBRARIES})
