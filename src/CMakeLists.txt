cmake_minimum_required(VERSION 3.15)
project(astock_src
VERSION 0.0.1
LANGUAGES CXX)
# message(STATUS "$ENV{CMAKE_PREFIX_PATH}")

# Qt build specific options, need to be defined before an Qt targets


add_subdirectory(foundation)
add_subdirectory(domain)
add_subdirectory(engine)
add_subdirectory(ui)
add_subdirectory(app)

