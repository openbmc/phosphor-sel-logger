/*
// Copyright (c) 2018 Intel Corporation
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
#include <sel_logger.hpp>
#include <sensorutils.hpp>

#include <string_view>
#include <variant>

enum class thresholdEventOffsets : uint8_t
{
    lowerNonCritGoingLow = 0x00,
    lowerCritGoingLow = 0x02,
    upperNonCritGoingHigh = 0x07,
    upperCritGoingHigh = 0x09,
};

static constexpr const uint8_t thresholdEventDataTriggerReadingByte2 = (1 << 6);
static constexpr const uint8_t thresholdEventDataTriggerReadingByte3 = (1 << 4);

static const std::string openBMCMessageRegistryVersion("0.1");

inline static sdbusplus::bus::match_t startThresholdAssertMonitor(
    std::shared_ptr<sdbusplus::asio::connection> conn)
{
    auto thresholdAssertMatcherCallback = [conn](sdbusplus::message_t& msg) {
        // This static set of std::pair<path, event> tracks asserted events to
        // avoid duplicate logs or deasserts logged without an assert
        static boost::container::flat_set<std::pair<std::string, std::string>>
            assertedEvents;
        std::vector<uint8_t> eventData(selEvtDataMaxSize,
                                       selEvtDataUnspecified);

        // Get the event type and assertion details from the message
        std::string sensorName;
        std::string thresholdInterface;
        std::string event;
        bool assert;
        double assertValue;
        try
        {
            msg.read(sensorName, thresholdInterface, event, assert,
                     assertValue);
        }
        catch (const sdbusplus::exception_t&)
        {
            std::cerr << "error getting assert signal data from "
                      << msg.get_path() << "\n";
            return;
        }

        // Check the asserted events to determine if we should log this event
        std::pair<std::string, std::string> pathAndEvent(
            std::string(msg.get_path()), event);
        if (assert)
        {
            // For asserts, add the event to the set and only log it if it's new
            if (assertedEvents.insert(pathAndEvent).second == false)
            {
                // event is already in the set
                return;
            }
        }
        else
        {
            // For deasserts, remove the event and only log the deassert if it
            // was asserted
            if (assertedEvents.erase(pathAndEvent) == 0)
            {
                // asserted event was not in the set
                return;
            }
        }

        // Set the IPMI threshold event type based on the event details from the
        // message
        if (event == "CriticalAlarmLow")
        {
            eventData[0] =
                static_cast<uint8_t>(thresholdEventOffsets::lowerCritGoingLow);
        }
        else if (event == "WarningAlarmLow")
        {
            eventData[0] = static_cast<uint8_t>(
                thresholdEventOffsets::lowerNonCritGoingLow);
        }
        else if (event == "WarningAlarmHigh")
        {
            eventData[0] = static_cast<uint8_t>(
                thresholdEventOffsets::upperNonCritGoingHigh);
        }
        else if (event == "CriticalAlarmHigh")
        {
            eventData[0] =
                static_cast<uint8_t>(thresholdEventOffsets::upperCritGoingHigh);
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
            sdbusplus::message_t getSensorValueResp =
                conn->call(getSensorValue);
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
        // Get the threshold parameter by removing the "Alarm" text from the
        // event string
        std::string alarm("Alarm");
        if (std::string::size_type pos = event.find(alarm);
            pos != std::string::npos)
        {
            event.erase(pos, alarm.length());
        }
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
            std::cerr << "error getting sensor threshold from "
                      << msg.get_path() << "\n";
            return;
        }
        double thresholdVal =
            std::visit(ipmi::VariantToDoubleVisitor(), thresholdValue);

        double scale = 0;
        auto findScale = sensorValue.find("Scale");
        if (findScale != sensorValue.end())
        {
            scale =
                std::visit(ipmi::VariantToDoubleVisitor(), findScale->second);
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

        std::string threshold;
        std::string direction;
        std::string redfishMessageID =
            "OpenBMC." + openBMCMessageRegistryVersion;
        enum EventType
        {
            eventNone,
            eventInfo,
            eventWarn,
            eventErr
        };
        EventType eventType = eventNone;
        if (event == "CriticalLow")
        {
            threshold = "critical low";
            if (assert)
            {
                eventType = eventErr;
                direction = "low";
                redfishMessageID += ".SensorThresholdCriticalLowGoingLow";
            }
            else
            {
                eventType = eventInfo;
                direction = "high";
                redfishMessageID += ".SensorThresholdCriticalLowGoingHigh";
            }
        }
        else if (event == "WarningLow")
        {
            threshold = "warning low";
            if (assert)
            {
                eventType = eventWarn;
                direction = "low";
                redfishMessageID += ".SensorThresholdWarningLowGoingLow";
            }
            else
            {
                eventType = eventInfo;
                direction = "high";
                redfishMessageID += ".SensorThresholdWarningLowGoingHigh";
            }
        }
        else if (event == "WarningHigh")
        {
            threshold = "warning high";
            if (assert)
            {
                eventType = eventWarn;
                direction = "high";
                redfishMessageID += ".SensorThresholdWarningHighGoingHigh";
            }
            else
            {
                eventType = eventInfo;
                direction = "low";
                redfishMessageID += ".SensorThresholdWarningHighGoingLow";
            }
        }
        else if (event == "CriticalHigh")
        {
            threshold = "critical high";
            if (assert)
            {
                eventType = eventErr;
                direction = "high";
                redfishMessageID += ".SensorThresholdCriticalHighGoingHigh";
            }
            else
            {
                eventType = eventInfo;
                direction = "low";
                redfishMessageID += ".SensorThresholdCriticalHighGoingLow";
            }
        }

        std::string journalMsg(std::string(sensorName) + " sensor crossed a " +
                               threshold + " threshold going " + direction +
                               ". Reading=" + std::to_string(assertValue) +
                               " Threshold=" + std::to_string(thresholdVal) +
                               ".");

#ifdef SEL_LOGGER_SEND_TO_LOGGING_SERVICE
        std::string LogLevel = "";
        switch (eventType)
        {
            case eventInfo:
            {
                LogLevel =
                    "xyz.openbmc_project.Logging.Entry.Level.Informational";
                break;
            }
            case eventWarn:
            {
                LogLevel = "xyz.openbmc_project.Logging.Entry.Level.Warning";
                break;
            }
            case eventErr:
            {
                LogLevel = "xyz.openbmc_project.Logging.Entry.Level.Critical";
                break;
            }
            default:
            {
                LogLevel = "xyz.openbmc_project.Logging.Entry.Level.Debug";
                break;
            }
        }
        if (eventType != eventNone)
        {
            sdbusplus::message::message AddToLog = conn->new_method_call(
                "xyz.openbmc_project.Logging", "/xyz/openbmc_project/logging",
                "xyz.openbmc_project.Logging.Create", "Create");
            AddToLog.append(journalMsg, LogLevel,
                            std::map<std::string, std::string>(
                                {{"SENSOR_PATH", std::string(msg.get_path())},
                                 {"EVENT", threshold},
                                 {"DIRECTION", direction},
                                 {"THRESHOLD", std::to_string(thresholdVal)},
                                 {"READING", std::to_string(assertValue)}}));
            conn->call(AddToLog);
        }
#else
        selAddSystemRecord(
            journalMsg, std::string(msg.get_path()), eventData, assert,
            selBMCGenID, "REDFISH_MESSAGE_ID=%s", redfishMessageID.c_str(),
            "REDFISH_MESSAGE_ARGS=%.*s,%f,%f", sensorName.length(),
            sensorName.data(), assertValue, thresholdVal);
#endif
    };
    sdbusplus::bus::match_t thresholdAssertMatcher(
        static_cast<sdbusplus::bus_t&>(*conn),
        "type='signal', member='ThresholdAsserted'",
        std::move(thresholdAssertMatcherCallback));
    return thresholdAssertMatcher;
}
