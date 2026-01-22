# CMake generated Testfile for 
# Source directory: G:/C++/AStockQuantEngine/src/engine
# Build directory: G:/C++/AStockQuantEngine/build/src/engine
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
if(CTEST_CONFIGURATION_TYPE MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
  add_test(engine_test "G:/C++/AStockQuantEngine/build/bin/Debug/engine_test.exe")
  set_tests_properties(engine_test PROPERTIES  _BACKTRACE_TRIPLES "G:/C++/AStockQuantEngine/src/engine/CMakeLists.txt;44;add_test;G:/C++/AStockQuantEngine/src/engine/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
  add_test(engine_test "G:/C++/AStockQuantEngine/build/bin/Release/engine_test.exe")
  set_tests_properties(engine_test PROPERTIES  _BACKTRACE_TRIPLES "G:/C++/AStockQuantEngine/src/engine/CMakeLists.txt;44;add_test;G:/C++/AStockQuantEngine/src/engine/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
  add_test(engine_test "G:/C++/AStockQuantEngine/build/bin/MinSizeRel/engine_test.exe")
  set_tests_properties(engine_test PROPERTIES  _BACKTRACE_TRIPLES "G:/C++/AStockQuantEngine/src/engine/CMakeLists.txt;44;add_test;G:/C++/AStockQuantEngine/src/engine/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
  add_test(engine_test "G:/C++/AStockQuantEngine/build/bin/RelWithDebInfo/engine_test.exe")
  set_tests_properties(engine_test PROPERTIES  _BACKTRACE_TRIPLES "G:/C++/AStockQuantEngine/src/engine/CMakeLists.txt;44;add_test;G:/C++/AStockQuantEngine/src/engine/CMakeLists.txt;0;")
else()
  add_test(engine_test NOT_AVAILABLE)
endif()
subdirs("src")
