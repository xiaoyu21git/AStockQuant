# 掘金C++ SDK 64位集成说明

1. 头文件路径：
   gmsdk/include
2. 库文件路径：
   gmsdk/lib/win64/gmsdk.lib
   运行时需 gmsdk/lib/win64/gmsdk.dll
3. CMake配置：

```
# 在 src/engine/CMakeLists.txt 适当位置添加：
include_directories("${CMAKE_SOURCE_DIR}/sdk-cpp-windows-32&64-3.8.10/gmsdk/include")
link_directories("${CMAKE_SOURCE_DIR}/sdk-cpp-windows-32&64-3.8.10/gmsdk/lib/win64")

add_executable(demo_gm_strategy demo_gm_strategy.cpp)
target_link_libraries(demo_gm_strategy gmsdk)
```

4. 运行时需将 gmsdk.dll 复制到可执行文件同目录。

5. 示例代码见 src/engine/demo_gm_strategy.cpp。
