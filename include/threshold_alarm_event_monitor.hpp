/*
// Copyright (c) 2021 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
*/

#pragma once
#include "threshold_event_monitor.hpp"

#include <boost/container/flat_map.hpp>
#include <sdbusplus/bus/match.hpp>
#include <sel_logger.hpp>
#include <sensorutils.hpp>

#include <map>
#include <string_view>
#include <variant>

using sdbusMatch = std::shared_ptr<sdbusplus::bus::match_t>;
static sdbusMatch warningLowAssertedMatcher;
static sdbusMatch warningLowDeassertedMatcher;
static sdbusMatch warningHighAssertedMatcher;
static sdbusMatch warningHighDeassertedMatcher;
static sdbusMatch criticalLowAssertedMatcher;
static sdbusMatch criticalLowDeassertedMatcher;
static sdbusMatch criticalHighAssertedMatcher;
static sdbusMatch criticalHighDeassertedMatcher;

static boost::container::flat_map<std::string, sdbusMatch> matchers = {
    {"WarningLowAlarmAsserted", warningLowAssertedMatcher},
    {"WarningLowAlarmDeasserted", warningLowDeassertedMatcher},
    {"WarningHighAlarmAsserted", warningHighAssertedMatcher},
    {"WarningHighAlarmDeasserted", warningHighDeassertedMatcher},
    {"CriticalLowAlarmAsserted", criticalLowAssertedMatcher},
    {"CriticalLowAlarmDeasserted", criticalLowDeassertedMatcher},
    {"CriticalHighAlarmAsserted", criticalHighAssertedMatcher},
    {"CriticalHighAlarmDeasserted", criticalHighDeassertedMatcher}};

void generateEvent(std::string signalName,
                   std::shared_ptr<sdbusplus::asio::connection> conn,
                   sdbusplus::message_t& msg)
{
    double assertValue;
    try
    {
        msg.read(assertValue);
    }
    catch (const sdbusplus::exception_t&)
    {
        std::cerr << "error getting assert signal data from " << msg.get_path()
                  << "\n";
        return;
    }

    std::string event;
    std::string thresholdInterface;
    std::string threshold;
    std::string direction;
    bool assert = false;
    std::vector<uint8_t> eventData(selEvtDataMaxSize, selEvtDataUnspecified);
    std::string redfishMessageID = "OpenBMC." + openBMCMessageRegistryVersion;

    if (signalName == "WarningLowAlarmAsserted" ||
        signalName == "WarningLowAlarmDeasserted")
    {
        event = "WarningLow";
        thresholdInterface = "xyz.openbmc_project.Sensor.Threshold.Warning";
        eventData[0] =
            static_cast<uint8_t>(thresholdEventOffsets::lowerNonCritGoingLow);
        threshold = "warning low";
        if (signalName == "WarningLowAlarmAsserted")
        {
            assert = true;
            direction = "low";
            redfishMessageID += ".SensorThresholdWarningLowGoingLow";
        }
        else if (signalName == "WarningLowAlarmDeasserted")
        {
            direction = "high";
            redfishMessageID += ".SensorThresholdWarningLowGoingHigh";
        }
    }
    else if (signalName == "WarningHighAlarmAsserted" ||
             signalName == "WarningHighAlarmDeasserted")
    {
        event = "WarningHigh";
        thresholdInterface = "xyz.openbmc_project.Sensor.Threshold.Warning";
        eventData[0] =
            static_cast<uint8_t>(thresholdEventOffsets::upperNonCritGoingHigh);
        threshold = "warning high";
        if (signalName == "WarningHighAlarmAsserted")
        {
            assert = true;
            direction = "high";
            redfishMessageID += ".SensorThresholdWarningHighGoingHigh";
        }
        else if (signalName == "WarningHighAlarmDeasserted")
        {
            direction = "low";
            redfishMessageID += ".SensorThresholdWarningHighGoingLow";
        }
    }
    else if (signalName == "CriticalLowAlarmAsserted" ||
             signalName == "CriticalLowAlarmDeasserted")
    {
        event = "CriticalLow";
        thresholdInterface = "xyz.openbmc_project.Sensor.Threshold.Critical";
        eventData[0] =
            static_cast<uint8_t>(thresholdEventOffsets::lowerCritGoingLow);
        threshold = "critical low";
        if (signalName == "CriticalLowAlarmAsserted")
        {
            assert = true;
            direction = "low";
            redfishMessageID += ".SensorThresholdCriticalLowGoingLow";
        }
        else if (signalName == "CriticalLowAlarmDeasserted")
        {
            direction = "high";
            redfishMessageID += ".SensorThresholdCriticalLowGoingHigh";
        }
    }
    else if (signalName == "CriticalHighAlarmAsserted" ||
             signalName == "CriticalHighAlarmDeasserted")
    {
        event = "CriticalHigh";
        thresholdInterface = "xyz.openbmc_project.Sensor.Threshold.Critical";
        eventData[0] =
            static_cast<uint8_t>(thresholdEventOffsets::upperCritGoingHigh);
        threshold = "critical high";
        if (signalName == "CriticalHighAlarmAsserted")
        {
            assert = true;
            direction = "high";
            redfishMessageID += ".SensorThresholdCriticalHighGoingHigh";
        }
        else if (signalName == "CriticalHighAlarmDeasserted")
        {
            direction = "low";
            redfishMessageID += ".SensorThresholdCriticalHighGoingLow";
        }
    }
    // Indicate that bytes 2 and 3 are threshold sensor trigger values
    eventData[0] |= thresholdEventDataTriggerReadingByte2 |
                    thresholdEventDataTriggerReadingByte3;

    // Get the sensor reading to put in the event data
    sdbusplus::message_t getSensorValue =
        conn->new_method_call(msg.get_sender(), msg.get_path(),
                              "org.freedesktop.DBus.Properties", "GetAll");
    getSensorValue.append("xyz.openbmc_project.Sensor.Value");
    boost::container::flat_map<std::string, std::variant<double, int64_t>>
        sensorValue;
    try
    {
        sdbusplus::message_t getSensorValueResp = conn->call(getSensorValue);
        getSensorValueResp.read(sensorValue);
    }
    catch (const sdbusplus::exception_t&)
    {
        std::cerr << "error getting sensor value from " << msg.get_path()
                  << "\n";
        return;
    }
    double max = 0;
    auto findMax = sensorValue.find("MaxValue");
    if (findMax != sensorValue.end())
    {
        max = std::visit(ipmi::VariantToDoubleVisitor(), findMax->second);
    }
    double min = 0;
    auto findMin = sensorValue.find("MinValue");
    if (findMin != sensorValue.end())
    {
        min = std::visit(ipmi::VariantToDoubleVisitor(), findMin->second);
    }

    try
    {
        eventData[1] = ipmi::getScaledIPMIValue(assertValue, max, min);
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what();
        eventData[1] = selEvtDataUnspecified;
    }

    // Get the threshold value to put in the event data
    sdbusplus::message_t getThreshold =
        conn->new_method_call(msg.get_sender(), msg.get_path(),
                              "org.freedesktop.DBus.Properties", "Get");
    getThreshold.append(thresholdInterface, event);
    std::variant<double, int64_t> thresholdValue;
    try
    {
        sdbusplus::message_t getThresholdResp = conn->call(getThreshold);
        getThresholdResp.read(thresholdValue);
    }
    catch (const sdbusplus::exception_t&)
    {
        std::cerr << "error getting sensor threshold from " << msg.get_path()
                  << "\n";
        return;
    }
    double thresholdVal =
        std::visit(ipmi::VariantToDoubleVisitor(), thresholdValue);

    double scale = 0;
    auto findScale = sensorValue.find("Scale");
    if (findScale != sensorValue.end())
    {
        scale = std::visit(ipmi::VariantToDoubleVisitor(), findScale->second);
        thresholdVal *= std::pow(10, scale);
    }
    try
    {
        eventData[2] = ipmi::getScaledIPMIValue(thresholdVal, max, min);
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what();
        eventData[2] = selEvtDataUnspecified;
    }

    std::string_view sensorName(msg.get_path());
    sensorName.remove_prefix(
        std::min(sensorName.find_last_of("/") + 1, sensorName.size()));

    std::string journalMsg(std::string(sensorName) + " sensor crossed a " +
                           threshold + " threshold going " + direction +
                           ". Reading=" + std::to_string(assertValue) +
                           " Threshold=" + std::to_string(thresholdVal) + ".");

    selAddSystemRecord(conn, journalMsg, std::string(msg.get_path()), eventData,
                       assert, selBMCGenID, "REDFISH_MESSAGE_ID=%s",
                       redfishMessageID.c_str(),
                       "REDFISH_MESSAGE_ARGS=%.*s,%f,%f", sensorName.length(),
                       sensorName.data(), assertValue, thresholdVal);
}

inline static void startThresholdAlarmMonitor(
    std::shared_ptr<sdbusplus::asio::connection> conn)
{
    for (auto iter = matchers.begin(); iter != matchers.end(); iter++)
    {
        iter->second = std::make_shared<sdbusplus::bus::match_t>(
            static_cast<sdbusplus::bus_t&>(*conn),
            "type='signal',member=" + iter->first,
            [conn, iter](sdbusplus::message_t& msg) {
                generateEvent(iter->first, conn, msg);
            });
    }
}
