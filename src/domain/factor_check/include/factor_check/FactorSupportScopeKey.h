#pragma once

#include <string>

namespace factor::check {

std::string md5Hex(const std::string& input);

std::string buildScopePayloadJson(const std::string& dataSourceMode,
                                  int selectedDatasetId,
                                  const std::string& startDate,
                                  const std::string& endDate,
                                  const std::string& cacheSnapshotJson);

std::string buildScopeKeyHexMd5(const std::string& dataSourceMode,
                                int selectedDatasetId,
                                const std::string& startDate,
                                const std::string& endDate,
                                const std::string& cacheSnapshotJson);

} // namespace factor::check
