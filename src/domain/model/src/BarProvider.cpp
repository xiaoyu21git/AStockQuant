// domain/model/BarProvider.cpp
#include "BarProvider.h"
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace domain::model {

std::vector<Bar> BarProvider::loadFromCsv(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + filename);
    }

    std::vector<Bar> bars;
    std::string line;

    // 假设第一行是表头
    std::getline(file, line);

    while (std::getline(file, line)) {
        std::istringstream ss(line);
        Bar bar;
        std::string token;

        std::getline(ss, token, ','); 
        bar.symbol = token;
        std::getline(ss, token, ','); 
        bar.time = std::stoll(token);
        std::getline(ss, token, ','); 
        bar.open = std::stod(token);
        std::getline(ss, token, ','); 
        bar.high = std::stod(token);
        std::getline(ss, token, ','); 
        bar.low = std::stod(token);
        std::getline(ss, token, ','); 
        bar.close = std::stod(token);
        std::getline(ss, token, ','); 
        bar.volume = std::stod(token);

        bars.push_back(bar);
    }

    return bars;
}

} // namespace domain::model
