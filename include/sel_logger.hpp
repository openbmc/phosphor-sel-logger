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
#include <sdbusplus/asio/connection.hpp>

#include <filesystem>
#include <string>

static constexpr const char* ipmiSelObject = "xyz.openbmc_project.Logging.IPMI";
static constexpr const char* ipmiSelPath = "/xyz/openbmc_project/Logging/IPMI";
static constexpr const char* ipmiSelAddInterface =
    "xyz.openbmc_project.Logging.IPMI";

// ID string generated using journalctl to include in the MESSAGE_ID field for
// SEL entries.  Helps with filtering SEL entries in the journal.
static constexpr const char* selMessageId = "b370836ccf2f4850ac5bee185b77893a";
static constexpr int selPriority = 5; // notice
static constexpr uint8_t selSystemType = 0x02;
static constexpr uint16_t selBMCGenID = 0x0020;
static constexpr uint16_t selInvalidRecID =
    std::numeric_limits<uint16_t>::max();
static constexpr size_t selEvtDataMaxSize = 3;
static constexpr size_t selOemDataMaxSize = 13;
static constexpr uint8_t selEvtDataUnspecified = 0xFF;

static const std::filesystem::path selLogDir = "/var/log";
static const std::string selLogFilename = "ipmi_sel";
#ifdef SEL_LOGGER_ENABLE_SEL_DELETE
static const std::string nextRecordFilename = "next_records";
#endif

void toHexStr(const std::vector<uint8_t>& data, std::string& hexStr);

template <typename... T>
uint16_t selAddSystemRecord(
    [[maybe_unused]] std::shared_ptr<sdbusplus::asio::connection> conn,
    [[maybe_unused]] const std::string& message, const std::string& path,
    const std::vector<uint8_t>& selData, const bool& assert,
    const uint16_t& genId, [[maybe_unused]] T&&... metadata)
{
    // Only 3 bytes of SEL event data are allowed in a system record
    if (selData.size() > selEvtDataMaxSize)
    {
        throw std::invalid_argument("Event data too large");
    }
    std::string selDataStr;
    toHexStr(selData, selDataStr);

#ifdef SEL_LOGGER_SEND_TO_LOGGING_SERVICE
    sdbusplus::message_t AddToLog = conn->new_method_call(
        "xyz.openbmc_project.Logging", "/xyz/openbmc_project/logging",
        "xyz.openbmc_project.Logging.Create", "Create");

    std::string journalMsg(
        message + " from " + path + ": " +
        " RecordType=" + std::to_string(selSystemType) +
        ", GeneratorID=" + std::to_string(genId) +
        ", EventDir=" + std::to_string(assert) + ", EventData=" + selDataStr);

    AddToLog.append(
        journalMsg, "xyz.openbmc_project.Logging.Entry.Level.Informational",
        std::map<std::string, std::string>(
            {{"SENSOR_PATH", path},
             {"GENERATOR_ID", std::to_string(genId)},
             {"RECORD_TYPE", std::to_string(selSystemType)},
             {"EVENT_DIR", std::to_string(assert)},
             {"SENSOR_DATA", selDataStr}}));
    conn->call(AddToLog);
    return 0;
#else
    unsigned int recordId = getNewRecordId();
    if (recordId < selInvalidRecID)
    {
        sd_journal_send(
            "MESSAGE=%s", message.c_str(), "PRIORITY=%i", selPriority,
            "MESSAGE_ID=%s", selMessageId, "IPMI_SEL_RECORD_ID=%d", recordId,
            "IPMI_SEL_RECORD_TYPE=%x", selSystemType,
            "IPMI_SEL_GENERATOR_ID=%x", genId, "IPMI_SEL_SENSOR_PATH=%s",
            path.c_str(), "IPMI_SEL_EVENT_DIR=%x", assert, "IPMI_SEL_DATA=%s",
            selDataStr.c_str(), std::forward<T>(metadata)..., NULL);
    }
    return recordId;
#endif
}
