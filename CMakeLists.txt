cmake_minimum_required(VERSION 3.24)

project(keepawake LANGUAGES CXX)

add_executable(keepawake WIN32
  src/app.cpp
)

target_compile_features(keepawake PRIVATE cxx_std_17)
