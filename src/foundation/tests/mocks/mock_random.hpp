#pragma once
#include <gmock/gmock.h>
#include "foundation/utils/random.hpp"

namespace foundation::test::mocks {

class MockRandom : public utils::Random {
public:
    MOCK_METHOD(int, getInt, (int min, int max), (override));
    MOCK_METHOD(double, getDouble, (double min, double max), (override));
    MOCK_METHOD(bool, getBool, (), (override));
    MOCK_METHOD(std::string, getString, (size_t length), (override));
    MOCK_METHOD(std::string, getAlphanumericString, (size_t length), (override));
    MOCK_METHOD(std::string, getHexString, (size_t length, bool uppercase), (override));
    MOCK_METHOD(std::string, getUUID, (), (override));
};

} // namespace foundation::test::mocks