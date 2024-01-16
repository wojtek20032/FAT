cmake_minimum_required(VERSION 3.23)
project(FAT C)

set(CMAKE_C_STANDARD 99)

add_executable(FAT main.c disk.c file_reader.h 0001natural_fat12_volume.img)
