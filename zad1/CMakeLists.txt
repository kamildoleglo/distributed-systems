cmake_minimum_required(VERSION 3.13)
project(zad1 C)

set(CMAKE_C_STANDARD 99)

add_executable(zad1 client.c client.h list.c list.h)
target_link_libraries(zad1 -lpthread -lrt)