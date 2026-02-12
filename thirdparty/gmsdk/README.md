# thirdparty/gmsdk

本目录用于存放掘金(gmsdk)官方头文件的适配与转发层，所有类型、接口、事件均100%基于官方头文件，无自造类型。

- GmApiWrapper.h：统一转发gmsdk全部官方头文件，便于业务层和事件总线直接调用。
- GmUnifiedAdapter.h：事件总线与业务层集成的统一适配层，支持gmsdk::Strategy等类型的直接继承与事件桥接。

## 适配原则
- 禁止自造类型，所有接口、结构体、事件、枚举均以gmsdk官方头文件为准。
- 适配层仅做include、命名空间适配和事件桥接，不做任何业务逻辑改造。
- 业务层如需自定义策略，直接继承gmsdk::Strategy。
- 事件总线集成可在GmUnifiedAdapter.h扩展。

## 依赖
- 需在CMakeLists.txt中添加：
  - include_directories(${LIBS_ROOT}/gmsdk/include)
  - include_directories(${CMAKE_SOURCE_DIR}/thirdparty/gmsdk)

