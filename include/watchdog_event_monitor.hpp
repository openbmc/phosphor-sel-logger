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

enum class watchdogEventOffsets : uint8_t
{
    noAction = 0x00,
    hardReset = 0x01,
    powerDown = 0x02,
    powerCycle = 0x03,
};

enum class watchdogTimerUseOffsets : uint8_t
{
   reserved = 0x00,
   BIOSFRB2 = 0x01,
   BIOSPOST = 0x02,
   OSLoad = 0x03,
   SMSOS = 0x04,
   OEM = 0x05,
   unspecified = 0x0f,
};

enum class watchdogInterruptTypeOffsets : uint8_t
{
    none = 0x00,
    SMI = 0x01,
    NMI = 0x02,
    messageInterrupt = 0x03,
    unspecified = 0x0f,
};


inline static sdbusplus::bus::match::match startWatchdogEventMonitor(
    std::shared_ptr<sdbusplus::asio::connection> conn)
{
    auto watchdogEventMatcherCallback = [conn](sdbusplus::message::message& msg) {

        std::vector<uint8_t> eventData(selEvtDataMaxSize, selEvtDataUnspecified);

        std::string watchdogInterface;
        boost::container::flat_map<std::string, std::variant<bool>>
            propertiesChanged;

        msg.read(watchdogInterface, propertiesChanged);

        std::string event = propertiesChanged.begin()->first;
        bool *pval = std::get_if<bool>(&propertiesChanged.begin()->second);

        if (!pval)
        {
          std::cerr << "watchdog event direction has invalid type\n";
          return;
        }
        bool assert = *pval;

        if (event != "Enabled")
        {
          return;
        }
        //get watchdog status porperties
        sdbusplus::message::message getWatchdogStatus =
          conn->new_method_call(msg.get_sender(), msg.get_path(),
                                "org.freedesktop.DBus.Properties", "GetAll");
        getWatchdogStatus.append("xyz.openbmc_project.State.Watchdog");
        boost::container::flat_map<std::string, std::variant<std::string , uint64_t, bool>> watchdogStatus;

        try
          {
          sdbusplus::message::message getWatchdogStatusResp =
            conn->call(getWatchdogStatus);
          getWatchdogStatusResp.read(watchdogStatus);
        }
        catch (sdbusplus::exception_t&)
        {
          std::cerr << "error getting watchdog status from " << msg.get_path()
                    << "\n";
          return;
        }

        auto getWatchdogEnabled = watchdogStatus.find("Enabled");
        bool watchdogEnabled;
        if (getWatchdogEnabled != watchdogStatus.end())
        {
          watchdogEnabled = std::get<bool>(getWatchdogEnabled->second);
          assert = watchdogEnabled;
        }

        auto getExpireAction = watchdogStatus.find("ExpireAction");
        std::string_view expireAction;
        if (getExpireAction != watchdogStatus.end())
        {
          expireAction = std::get<std::string>(getExpireAction->second);
          expireAction.remove_prefix(std::min(expireAction.find_last_of(".") + 1, expireAction.size()));
        }

        if (expireAction == "HardReset")
        {
          eventData[0] = static_cast<uint8_t>(watchdogEventOffsets::hardReset);
        }
        else if (expireAction == "PowerOff")
        {
          eventData[0] = static_cast<uint8_t>(watchdogEventOffsets::powerDown);
        }
        else if (expireAction == "PowerCycle")
        {
          eventData[0] = static_cast<uint8_t>(watchdogEventOffsets::powerCycle);
        }
        else if (expireAction == "None")
        {
          eventData[0] = static_cast<uint8_t>(watchdogEventOffsets::noAction);
        }

        auto getPreTimeoutInterrupt = watchdogStatus.find("PreTimeoutInterrupt");
        std::string_view preTimeoutInterrupt;
        if (getPreTimeoutInterrupt != watchdogStatus.end())
        {
          preTimeoutInterrupt = std::get<std::string>(getPreTimeoutInterrupt->second);
          preTimeoutInterrupt.remove_prefix(std::min(preTimeoutInterrupt.find_last_of(".") + 1, preTimeoutInterrupt.size()));
        }

        if (preTimeoutInterrupt == "None")
        {
          eventData[1] &= ( static_cast<uint8_t>(watchdogInterruptTypeOffsets::none) << 4);
        }
        else if (preTimeoutInterrupt == "SMI")
        {
          eventData[1] &= ( static_cast<uint8_t>(watchdogInterruptTypeOffsets::SMI) << 4);
        }
        else if (preTimeoutInterrupt == "NMI")
        {
          eventData[1] &= ( static_cast<uint8_t>(watchdogInterruptTypeOffsets::NMI) << 4);
        }
        else if (preTimeoutInterrupt == "MI")
        {
          eventData[1] &= ( static_cast<uint8_t>(watchdogInterruptTypeOffsets::messageInterrupt) << 4);
        }

        auto getCurrentTimerUse = watchdogStatus.find("CurrentTimerUse");
        std::string_view currentTimerUse;
        if (getCurrentTimerUse != watchdogStatus.end())
        {
          currentTimerUse = std::get<std::string>(getCurrentTimerUse->second);
          currentTimerUse.remove_prefix(std::min(currentTimerUse.find_last_of(".") + 1, currentTimerUse.size()));
        }

        if (currentTimerUse == "BIOSFRB2")
        {
          eventData[1] |= static_cast<uint8_t>(watchdogTimerUseOffsets::BIOSFRB2);
        }
        else if (currentTimerUse == "BIOSPOST")
        {
          eventData[1] |= static_cast<uint8_t>(watchdogTimerUseOffsets::BIOSPOST);
        }
        else if (currentTimerUse == "OSLoad")
        {
          eventData[1] |= static_cast<uint8_t>(watchdogTimerUseOffsets::OSLoad);
        }
        else if (currentTimerUse == "SMSOS")
        {
          eventData[1] |= static_cast<uint8_t>(watchdogTimerUseOffsets::SMSOS);
        }
        else if (currentTimerUse == "OEM")
        {
          eventData[1] |= static_cast<uint8_t>(watchdogTimerUseOffsets::OEM);
        }
        else
        {
          eventData[1] |= static_cast<uint8_t>(watchdogTimerUseOffsets::unspecified);
        }

        auto getWatchdogInterval = watchdogStatus.find("Interval");
        uint64_t watchdogInterval;
        if (getWatchdogInterval != watchdogStatus.end())
        {
          watchdogInterval = std::get<uint64_t>(getWatchdogInterval->second);
        }

        std::string direction;
        if (assert)
        {
          direction = " enable " ;
        }
        else
        {
          direction = " disable ";
        }

        // Construct a human-readable message of this event for the log
        std::string journalMsg(std::string(currentTimerUse) +
                               std::string(direction) +
                               "watchdog countdown " +
                               std::to_string(watchdogInterval/1000) +
                               " seconds "+
                               std::string(expireAction) +
                               " action");
        //TODO: need confirm what's redfishMessageID, the current naming by sensor type
        std::string redfishMessageID="watchdog2";
        std::string sensorName="watchdog";

        selAddSystemRecord(journalMsg, std::string(msg.get_path()), eventData,
                           assert, selBMCGenID, "REDFISH_MESSAGE_ID=%.*s",redfishMessageID.length(),
                           redfishMessageID.data(),"REDFISH_MESSAGE_ARG_1=%.*s", sensorName.length(),sensorName.data());
    };
    sdbusplus::bus::match::match watchdogEventMatcher(
        static_cast<sdbusplus::bus::bus&>(*conn),
        "type='signal',interface='org.freedesktop.DBus.Properties',member='"
        "PropertiesChanged',arg0namespace='xyz.openbmc_project.State."
        "Watchdog'",
        std::move(watchdogEventMatcherCallback));
    return watchdogEventMatcher;
}
