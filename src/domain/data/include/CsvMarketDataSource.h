#pragma once
#include <string>
#include "IMarketDataSource.h"

namespace domain::data {

class CsvMarketDataSource : public IMarketDataSource {
public:
    explicit CsvMarketDataSource(std::string filePath);

    std::vector<domain::model::Bar> loadBars() override;

private:
    std::string m_filePath;
};

}
