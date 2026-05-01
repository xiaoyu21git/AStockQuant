#pragma once

#include <Eigen/Dense>

#include <string>
#include <unordered_map>
#include <vector>

namespace factor {

std::unordered_map<std::string, double> batchCalculateRsi(
    const std::unordered_map<std::string, std::vector<double>>& allCloses,
    int period);

std::unordered_map<std::string, double> batchCalculateMacd(
    const std::unordered_map<std::string, std::vector<double>>& allCloses,
    int fast,
    int slow,
    int signal);

std::unordered_map<std::string, double> batchCalculateMa(
    const std::unordered_map<std::string, std::vector<double>>& allCloses,
    int period);

std::unordered_map<std::string, double> batchCalculateEma(
    const std::unordered_map<std::string, std::vector<double>>& allCloses,
    int period);

std::unordered_map<std::string, double> batchCalculateBoll(
    const std::unordered_map<std::string, std::vector<double>>& allCloses,
    int period,
    double stdMultiplier = 2.0);

std::unordered_map<std::string, double> batchCalculateKdj(
    const std::unordered_map<std::string, std::vector<double>>& allHighs,
    const std::unordered_map<std::string, std::vector<double>>& allLows,
    const std::unordered_map<std::string, std::vector<double>>& allCloses,
    int window,
    int kPeriod,
    int dPeriod);

std::unordered_map<std::string, double> batchCalculateAtr(
    const std::unordered_map<std::string, std::vector<double>>& allHighs,
    const std::unordered_map<std::string, std::vector<double>>& allLows,
    const std::unordered_map<std::string, std::vector<double>>& allCloses,
    int window);

std::unordered_map<std::string, double> batchCalculateVwap(
    const std::unordered_map<std::string, std::vector<double>>& allCloses,
    const std::unordered_map<std::string, std::vector<double>>& allVolumes);

std::unordered_map<std::string, double> batchCalculateVolumeRatio(
    const std::unordered_map<std::string, std::vector<double>>& allVolumes,
    int period);

std::unordered_map<std::string, double> batchCalculateObv(
    const std::unordered_map<std::string, std::vector<double>>& allCloses,
    const std::unordered_map<std::string, std::vector<double>>& allVolumes);

std::unordered_map<std::string, double> batchCalculateTurnoverStability(
    const std::unordered_map<std::string, std::vector<double>>& allValues);

} // namespace factor