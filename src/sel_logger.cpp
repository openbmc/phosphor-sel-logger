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
#include <systemd/sd-journal.h>

#include <boost/container/flat_map.hpp>
#include <boost/container/flat_set.hpp>
#include <experimental/string_view>
#include <iomanip>
#include <iostream>
#include <sdbusplus/asio/object_server.hpp>
#include <sel_logger.hpp>
#include <sstream>
#include <threshold_event_monitor.hpp>
#include <watchdog_event_monitor.hpp>

struct DBusInternalError final : public sdbusplus::exception_t
{
    const char *name() const noexcept override
    {
        return "org.freedesktop.DBus.Error.Failed";
    };
    const char *description() const noexcept override
    {
        return "internal error";
    };
    const char *what() const noexcept override
    {
        return "org.freedesktop.DBus.Error.Failed: "
               "internal error";
    };
};

static unsigned int initializeRecordId(void)
{
    int journalError = -1;
    sd_journal *journal;
    journalError = sd_journal_open(&journal, SD_JOURNAL_LOCAL_ONLY);
    if (journalError < 0)
    {
        std::cerr << "Failed to open journal: " << strerror(-journalError)
                  << "\n";
        throw DBusInternalError();
    }
    unsigned int recordId = selInvalidRecID;
    char match[256] = {};
    snprintf(match, sizeof(match), "MESSAGE_ID=%s", selMessageId);
    sd_journal_add_match(journal, match, 0);
    SD_JOURNAL_FOREACH_BACKWARDS(journal)
    {
        const char *data = nullptr;
        size_t length;

        journalError = sd_journal_get_data(journal, "IPMI_SEL_RECORD_ID",
                                           (const void **)&data, &length);
        if (journalError < 0)
        {
            std::cerr << "Failed to read IPMI_SEL_RECORD_ID field: "
                      << strerror(-journalError) << "\n";
            continue;
        }
        if (journalError =
                sscanf(data, "IPMI_SEL_RECORD_ID=%u", &recordId) != 1)
        {
            std::cerr << "Failed to parse record ID: " << journalError << "\n";
            throw DBusInternalError();
        }
        break;
    }
    sd_journal_close(journal);
    return recordId;
}

static unsigned int getNewRecordId(void)
{
    static unsigned int recordId = initializeRecordId();

    if (++recordId >= selInvalidRecID)
    {
        recordId = 1;
    }
    return recordId;
}

static void toHexStr(const std::vector<uint8_t> &data, std::string &hexStr)
{
    std::stringstream stream;
    stream << std::hex << std::uppercase << std::setfill('0');
    for (const int &v : data)
    {
        stream << std::setw(2) << v;
    }
    hexStr = stream.str();
}

template <typename... T>
static uint16_t
    selAddSystemRecord(const std::string &message, const std::string &path,
                       const std::vector<uint8_t> &selData, const bool &assert,
                       const uint16_t &genId, T &&... metadata)
{
    // Only 3 bytes of SEL event data are allowed in a system record
    if (selData.size() > selEvtDataMaxSize)
    {
        throw std::invalid_argument("Event data too large");
    }
    std::string selDataStr;
    toHexStr(selData, selDataStr);

    unsigned int recordId = getNewRecordId();
    sd_journal_send("MESSAGE=%s", message.c_str(), "PRIORITY=%i", selPriority,
                    "MESSAGE_ID=%s", selMessageId, "IPMI_SEL_RECORD_ID=%d",
                    recordId, "IPMI_SEL_RECORD_TYPE=%x", selSystemType,
                    "IPMI_SEL_GENERATOR_ID=%x", genId,
                    "IPMI_SEL_SENSOR_PATH=%s", path.c_str(),
                    "IPMI_SEL_EVENT_DIR=%x", assert, "IPMI_SEL_DATA=%s",
                    selDataStr.c_str(), std::forward<T>(metadata)..., NULL);
    return recordId;
}

static uint16_t selAddOemRecord(const std::string &message,
                                const std::vector<uint8_t> &selData,
                                const uint8_t &recordType)
{
    // A maximum of 13 bytes of SEL event data are allowed in an OEM record
    if (selData.size() > selOemDataMaxSize)
    {
        throw std::invalid_argument("Event data too large");
    }
    std::string selDataStr;
    toHexStr(selData, selDataStr);

    unsigned int recordId = getNewRecordId();
    sd_journal_send("MESSAGE=%s", message.c_str(), "PRIORITY=%i", selPriority,
                    "MESSAGE_ID=%s", selMessageId, "IPMI_SEL_RECORD_ID=%d",
                    recordId, "IPMI_SEL_RECORD_TYPE=%x", recordType,
                    "IPMI_SEL_DATA=%s", selDataStr.c_str(), NULL);
    return recordId;
}

int main(int argc, char *argv[])
{
    // setup connection to dbus
    boost::asio::io_service io;
    auto conn = std::make_shared<sdbusplus::asio::connection>(io);

    // IPMI SEL Object
    conn->request_name(ipmiSelObject);
    auto server = sdbusplus::asio::object_server(conn);

    // Add SEL Interface
    std::shared_ptr<sdbusplus::asio::dbus_interface> ifaceAddSel =
        server.add_interface(ipmiSelPath, ipmiSelAddInterface);

    // Add a new SEL entry
    ifaceAddSel->register_method(
        "IpmiSelAdd", [](const std::string &message, const std::string &path,
                         const std::vector<uint8_t> &selData,
                         const bool &assert, const uint16_t &genId) {
            return selAddSystemRecord(message, path, selData, assert, genId);
        });
    // Add a new OEM SEL entry
    ifaceAddSel->register_method(
        "IpmiSelAddOem",
        [](const std::string &message, const std::vector<uint8_t> &selData,
           const uint8_t &recordType) {
            return selAddOemRecord(message, selData, recordType);
        });
    ifaceAddSel->initialize();

#ifdef SEL_LOGGER_MONITOR_THRESHOLD_EVENTS
    sdbusplus::bus::match::match thresholdEventMonitor =
        startThresholdEventMonitor(conn);
#endif

#ifdef SEL_LOGGER_MONITOR_WATCHDOG_EVENTS
    sdbusplus::bus::match::match watchdogEventMonitor =
        startWatchdogEventMonitor(conn);
#endif
    io.run();

    return 0;
}
