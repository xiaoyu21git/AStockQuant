#include "factor_compute/FactorOperatorLibrary.h"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace factor::compute {

namespace {

constexpr int32_t kMinimumWindow = 1;
constexpr double kMissingValue = std::numeric_limits<double>::quiet_NaN();

[[nodiscard]] bool hasCompatibleShape(
    NumericConstMatrixView input,
    NumericMatrixView output) noexcept
{
    return input.rowCount == output.rowCount && input.columnCount == output.columnCount;
}

[[nodiscard]] size_t toFlatIndex(int32_t rowIndex, int32_t columnIndex, int32_t rowStride) noexcept
{
    return static_cast<size_t>(rowIndex) * static_cast<size_t>(rowStride)
        + static_cast<size_t>(columnIndex);
}

void requireValidViews(NumericConstMatrixView input, NumericMatrixView output)
{
    if (!input.isValid() || !output.isValid() || !hasCompatibleShape(input, output)) {
        std::abort();
    }
}

void requireValidWindow(int32_t window)
{
    if (window < kMinimumWindow) {
        std::abort();
    }
}

} // namespace

void FactorOperatorLibrary::lag(NumericConstMatrixView input, int32_t window, NumericMatrixView output) const
{
    requireValidViews(input, output);
    requireValidWindow(window);

    for (int32_t row = 0; row < input.rowCount; ++row) {
        for (int32_t col = 0; col < input.columnCount; ++col) {
            const size_t outIndex = toFlatIndex(row, col, output.rowStride);
            if (row < window) {
                output.data[outIndex] = kMissingValue;
                continue;
            }

            const size_t inIndex = toFlatIndex(row - window, col, input.rowStride);
            output.data[outIndex] = input.data[inIndex];
        }
    }
}

void FactorOperatorLibrary::rollingMean(NumericConstMatrixView input, int32_t window, NumericMatrixView output) const
{
    requireValidViews(input, output);
    requireValidWindow(window);

    for (int32_t row = 0; row < input.rowCount; ++row) {
        for (int32_t col = 0; col < input.columnCount; ++col) {
            const size_t outIndex = toFlatIndex(row, col, output.rowStride);
            if (row + 1 < window) {
                output.data[outIndex] = kMissingValue;
                continue;
            }

            double sum = 0.0;
            for (int32_t offset = 0; offset < window; ++offset) {
                const int32_t sourceRow = row - offset;
                const size_t inIndex = toFlatIndex(sourceRow, col, input.rowStride);
                sum += input.data[inIndex];
            }
            output.data[outIndex] = sum / static_cast<double>(window);
        }
    }
}

void FactorOperatorLibrary::rollingSum(NumericConstMatrixView input, int32_t window, NumericMatrixView output) const
{
    requireValidViews(input, output);
    requireValidWindow(window);

    for (int32_t row = 0; row < input.rowCount; ++row) {
        for (int32_t col = 0; col < input.columnCount; ++col) {
            const size_t outIndex = toFlatIndex(row, col, output.rowStride);
            if (row + 1 < window) {
                output.data[outIndex] = kMissingValue;
                continue;
            }

            double sum = 0.0;
            for (int32_t offset = 0; offset < window; ++offset) {
                const int32_t sourceRow = row - offset;
                const size_t inIndex = toFlatIndex(sourceRow, col, input.rowStride);
                sum += input.data[inIndex];
            }
            output.data[outIndex] = sum;
        }
    }
}

void FactorOperatorLibrary::rank(NumericConstMatrixView input, NumericMatrixView output) const
{
    requireValidViews(input, output);

    std::vector<std::pair<double, int32_t>> rowBuffer;
    rowBuffer.reserve(static_cast<size_t>(input.columnCount));

    for (int32_t row = 0; row < input.rowCount; ++row) {
        rowBuffer.clear();
        for (int32_t col = 0; col < input.columnCount; ++col) {
            const size_t inIndex = toFlatIndex(row, col, input.rowStride);
            rowBuffer.emplace_back(input.data[inIndex], col);
        }

        std::stable_sort(
            rowBuffer.begin(),
            rowBuffer.end(),
            [](const auto& left, const auto& right) { return left.first < right.first; });

        for (int32_t rankIndex = 0; rankIndex < input.columnCount; ++rankIndex) {
            const int32_t originalCol = rowBuffer[static_cast<size_t>(rankIndex)].second;
            const size_t outIndex = toFlatIndex(row, originalCol, output.rowStride);
            output.data[outIndex] = static_cast<double>(rankIndex + 1);
        }
    }
}

void FactorOperatorLibrary::groupByMean(
    NumericConstMatrixView input,
    GroupKeyView groupKeys,
    NumericMatrixView output) const
{
    requireValidViews(input, output);
    if (!groupKeys.isValid() || groupKeys.count != input.columnCount) {
        std::abort();
    }

    for (int32_t row = 0; row < input.rowCount; ++row) {
        std::unordered_map<uint32_t, std::pair<double, int32_t>> groupStats;
        groupStats.reserve(static_cast<size_t>(input.columnCount));

        for (int32_t col = 0; col < input.columnCount; ++col) {
            const uint32_t groupKey = groupKeys.data[col];
            const size_t inIndex = toFlatIndex(row, col, input.rowStride);
            auto& stats = groupStats[groupKey];
            stats.first += input.data[inIndex];
            stats.second += 1;
        }

        for (int32_t col = 0; col < input.columnCount; ++col) {
            const uint32_t groupKey = groupKeys.data[col];
            const auto statsIt = groupStats.find(groupKey);
            if (statsIt == groupStats.end() || statsIt->second.second == 0) {
                std::abort();
            }

            const double mean = statsIt->second.first / static_cast<double>(statsIt->second.second);
            const size_t outIndex = toFlatIndex(row, col, output.rowStride);
            output.data[outIndex] = mean;
        }
    }
}

} // namespace factor::compute


