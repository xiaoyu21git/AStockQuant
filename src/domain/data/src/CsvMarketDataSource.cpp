#include "CsvMarketDataSource.h"
#include <fstream>
#include <sstream>

namespace domain::data {

CsvMarketDataSource::CsvMarketDataSource(std::string filePath)
    : m_filePath(std::move(filePath)) {}

std::vector<domain::model::Bar> CsvMarketDataSource::loadBars() {
    std::vector<domain::model::Bar> bars;
    std::ifstream file(m_filePath);

    if (!file.is_open())
        return bars;

    std::string line;
    std::getline(file, line); // skip header

    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string item;

        domain::model::Bar bar;

        std::getline(ss, bar.symbol, ',');
        std::getline(ss, item, ','); 
        bar.time = std::stoll(item);
        std::getline(ss, item, ','); 
        bar.open  = std::stod(item);
        std::getline(ss, item, ','); 
        bar.high  = std::stod(item);
        std::getline(ss, item, ','); 
        bar.low   = std::stod(item);
        std::getline(ss, item, ','); 
        bar.close = std::stod(item);
        std::getline(ss, item, ','); 
        bar.volume = std::stod(item);

        bar.time = bar.time;

        if (bar.isValid())
            bars.push_back(bar);
    }

    return bars;
}

}
