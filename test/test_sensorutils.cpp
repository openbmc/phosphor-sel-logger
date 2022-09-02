#include <sensorutils.hpp>

#include <tuple>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

constexpr bool debug = false;

// Min, Val, Max
using Param = std::tuple<double, double, double>;
using namespace ipmi;

static double scaledIPMIValue2Double(const uint8_t value, const int16_t mValue,
                                     const int8_t rExp, const int16_t bValue,
                                     const int8_t bExp)
{
    // y = (Mx + (B * 10^(bExp))) * 10^(rExp)
    return static_cast<double>((mValue * value) +
                               (bValue * std::pow(10.0, bExp))) *
           std::pow(10.0, rExp);
}

static void testScaleIPMIValue(const Param& param)
{
    const auto& [min, val, max] = param;

    int16_t mValue = 0;
    int8_t rExp = 0;
    int16_t bValue = 0;
    int8_t bExp = 0;

    double fullRange = max - min;
    double tolerance = ((1 / 255.0) * fullRange);

    if constexpr (debug)
    {
        std::cout << "tolerance: " << tolerance << std::endl;
    }

    // based the min/max to calculate the m, rExp, bVal, bExp
    bool result = getSensorAttributes(max, min, mValue, rExp, bValue, bExp);
    if (!result)
    {
        throw std::runtime_error("Illegal sensor attributes");
    }

    if constexpr (debug)
    {
        std::cout << "mValue: " << (int)mValue << " rExp: " << (int)rExp
                  << " bValue: " << (int)bValue << " bExp: " << (int)bExp
                  << std::endl;
    }

    auto scaledVal = scaleIPMIValueFromDouble(val, mValue, rExp, bValue, bExp);
    double dCalVal =
        scaledIPMIValue2Double(scaledVal, mValue, rExp, bValue, bExp);

    if constexpr (debug)
    {
        std::cout << "calculated: " << dCalVal << std::endl;
    }

    // EXPECT there's deviation not less than 5%
    EXPECT_GE(val + tolerance, dCalVal); // val + tolerance > dCalVal
    EXPECT_LE(val - tolerance, dCalVal); // val - tolerance < dCalVal
}

TEST(ScaleTest, GoodTestNegativeOnly)
{
    // random mock some negative numbers only, Min, Val, Max
    const std::vector<Param> params = {
        {-10, -1, -1},          {-100, -5, -1},
        {-127, -10, -1},        {-128, -99, -1},
        {-180, -19, -10},       {-250, -100, -50},
        {-2500, -120, -50},     {-12.3, -5.9, -0.08},
        {-10000, -5.9, -0.212}, {-1000, -103.22, -0.2122},
    };

    for (const auto& param : params)
    {
        testScaleIPMIValue(param);
    }
}

TEST(ScaleTest, GoodTestPositiveOnly)
{
    // random mock some positive numbers only, Min, Val, Max
    const std::vector<Param> params = {
        {1, 1, 10},          {1, 5, 100},
        {0, 1, 255},         {1, 5, 254},
        {10, 19, 180},       {50, 100, 250},
        {50, 120, 2500},     {0.08, 5.9, 12.3},
        {0.212, 5.9, 10000}, {0.2122, 103.22, 1000},
    };

    for (const auto& param : params)
    {
        testScaleIPMIValue(param);
    }
}

TEST(ScaleTest, GoodTestPositiveNegative)
{
    // random mock some positive and negative numbers, Min, Val, Max
    const std::vector<Param> params = {
        {-10, 1, 10},         {-100, 5, 100},        {-180, 19, 180},
        {-250, 100, 250},     {-2500, 120, 2500},    {-12.3, 5.9, 12.3},
        {-10000, 5.9, 10000}, {-1000, 103.22, 1000}, {-1000, -1, 1000},
    };

    for (const auto& param : params)
    {
        testScaleIPMIValue(param);
    }
}

TEST(ScaleTest, BadTest)
{
    // random mock some positive and negative numbers, Min, Val, Max
    const std::vector<Param> params = {
        {10, 1, 10},         {100, 5, 100},        {180, 19, 180},
        {250, 100, 250},     {2500, 120, 2500},    {12.3, 5.9, 12.3},
        {10000, 5.9, 10000}, {1000, 103.22, 1000}, {1000, -1, 1000},
    };

    for (const auto& param : params)
    {
        EXPECT_THROW(testScaleIPMIValue(param), std::runtime_error);
    }
}