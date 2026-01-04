// domain/model/BarProvider.h
#pragma once
#include "Bar.h"
#include <vector>
#include <string>

namespace domain::model {

class BarProvider {
public:
    // 从 CSV 文件加载
    static std::vector<Bar> loadFromCsv(const std::string& filename);

    // 可扩展：从 JSON / 数据库加载
};

} // namespace domain::model
