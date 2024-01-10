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

#include <boost/algorithm/string.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/container/flat_map.hpp>
#include <boost/container/flat_set.hpp>
#include <pulse_event_monitor.hpp>
#include <sdbusplus/asio/object_server.hpp>
#include <sel_logger.hpp>
#include <threshold_event_monitor.hpp>
#include <watchdog_event_monitor.hpp>
#ifdef SEL_LOGGER_MONITOR_THRESHOLD_ALARM_EVENTS
#include <threshold_alarm_event_monitor.hpp>
#endif
#ifdef SEL_LOGGER_MONITOR_HOST_ERROR_EVENTS
#include <host_error_event_monitor.hpp>
#endif

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

struct DBusInternalError final : public sdbusplus::exception_t
{
    const char* name() const noexcept override
    {
        return "org.freedesktop.DBus.Error.Failed";
    }
    const char* description() const noexcept override
    {
        return "internal error";
    }
    const char* what() const noexcept override
    {
        return "org.freedesktop.DBus.Error.Failed: "
               "internal error";
    }

    int get_errno() const noexcept override
    {
        return EACCES;
    }
};

#ifndef SEL_LOGGER_SEND_TO_LOGGING_SERVICE
static bool getSELLogFiles(std::vector<std::filesystem::path>& selLogFiles)
{
    // Loop through the directory looking for ipmi_sel log files
    for (const std::filesystem::directory_entry& dirEnt :
         std::filesystem::directory_iterator(selLogDir))
    {
        std::string filename = dirEnt.path().filename();
        if (boost::starts_with(filename, selLogFilename))
        {
            // If we find an ipmi_sel log file, save the path
            selLogFiles.emplace_back(selLogDir / filename);
        }
    }
    // As the log files rotate, they are appended with a ".#" that is higher for
    // the older logs. Since we don't expect more than 10 log files, we
    // can just sort the list to get them in order from newest to oldest
    std::sort(selLogFiles.begin(), selLogFiles.end());

    return !selLogFiles.empty();
}

static unsigned int initializeRecordId(void)
{
    std::vector<std::filesystem::path> selLogFiles;
    if (!getSELLogFiles(selLogFiles))
    {
        return selInvalidRecID;
    }
    std::ifstream logStream(selLogFiles.front());
    if (!logStream.is_open())
    {
        return selInvalidRecID;
    }
    std::string line;
    std::string newestEntry;
    while (std::getline(logStream, line))
    {
        newestEntry = line;
    }

    std::vector<std::string> newestEntryFields;
    boost::split(newestEntryFields, newestEntry, boost::is_any_of(" ,"),
                 boost::token_compress_on);
    if (newestEntryFields.size() < 4)
    {
        return selInvalidRecID;
    }

    return std::stoul(newestEntryFields[1]);
}

static unsigned int recordId = initializeRecordId();

static void saveClearSelTimestamp()
{
    int fd = open("/var/lib/ipmi/sel_erase_time",
                  O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0)
    {
        std::cerr << "Failed to open file\n";
        return;
    }

    if (futimens(fd, NULL) < 0)
    {
        std::cerr << "Failed to update SEL cleared timestamp: "
                  << std::string(strerror(errno));
    }
    close(fd);
}

void clearSelLogFiles()
{
    saveClearSelTimestamp();

    // Clear the SEL by deleting the log files
    std::vector<std::filesystem::path> selLogFiles;
    if (getSELLogFiles(selLogFiles))
    {
        for (const std::filesystem::path& file : selLogFiles)
        {
            std::error_code ec;
            std::filesystem::remove(file, ec);
        }
    }

    freedRecordIDs.clear();
    recordId = selInvalidRecID;

    // Reload rsyslog so it knows to start new log files
    boost::asio::io_context io;
    auto dbus = std::make_shared<sdbusplus::asio::connection>(io);
    sdbusplus::message_t rsyslogReload = dbus->new_method_call(
        "org.freedesktop.systemd1", "/org/freedesktop/systemd1",
        "org.freedesktop.systemd1.Manager", "ReloadUnit");
    rsyslogReload.append("rsyslog.service", "replace");
    try
    {
        sdbusplus::message_t reloadResponse = dbus->call(rsyslogReload);
    }
    catch (const sdbusplus::exception_t& e)
    {
        std::cerr << e.what() << "\n";
    }
}

static bool selDeleteTargetRecord(const uint16_t& targetId)
{
    bool targetEntryFound = false;
    // Check if the ipmi_sel exist and save the path
    std::vector<std::filesystem::path> selLogFiles;
    if (!getSELLogFiles(selLogFiles))
    {
        return targetEntryFound;
    }

    // Go over all the ipmi_sel files to remove the entry with the target ID
    for (const std::filesystem::path& file : selLogFiles)
    {
        std::fstream logStream(file, std::ios::in);
        std::fstream tempFile(selLogDir / "temp", std::ios::out);
        if (!logStream.is_open())
        {
            return targetEntryFound;
        }
        std::string line;
        while (std::getline(logStream, line))
        {
            // Get the recordId of the current entry
            int left = line.find(" ");
            int right = line.find(",");
            int recordLen = right - left;
            std::string recordId = line.substr(left, recordLen);
            int newRecordId = std::stoi(recordId);

            if (newRecordId != targetId)
            {
                // Copy the entry from the original ipmi_sel to the temp file
                tempFile << line << '\n';
            }
            else
            {
                // Skip copying the target entry
                targetEntryFound = true;
            }
        }
        logStream.close();
        tempFile.close();
        if (targetEntryFound)
        {
            std::fstream logStream(file, std::ios::out);
            std::fstream tempFile(selLogDir / "temp", std::ios::in);
            while (std::getline(tempFile, line))
            {
                logStream << line << '\n';
            }
            logStream.close();
            tempFile.close();
            std::error_code ec;
            if (!std::filesystem::remove(selLogDir / "temp", ec))
            {
                std::cerr << ec.message() << std::endl;
            }
            break;
        }
    }
    return targetEntryFound;
}

static uint16_t selDeleteRecord(const uint16_t& targetId)
{
    std::filesystem::file_time_type prevAddTime =
        std::filesystem::last_write_time(selLogDir / selLogFilename);
    bool targetEntryFound = selDeleteTargetRecord(targetId);

    // Check if the targetId is Invalid
    if (!targetEntryFound)
    {
        return selInvalidRecID;
    }
    freedRecordIDs.emplace_back(targetId);
    std::filesystem::last_write_time(selLogDir / selLogFilename, prevAddTime);
    saveClearSelTimestamp();
    return targetId;
}

static unsigned int getNewRecordId(void)
{
    if (!freedRecordIDs.empty())
    {
        unsigned int freeRecord = freedRecordIDs.front();
        freedRecordIDs.pop_front();
        return freeRecord;
    }
    if (++recordId >= selInvalidRecID)
    {
        recordId = 1;
    }
    return recordId;
}
#endif

static void toHexStr(const std::vector<uint8_t>& data, std::string& hexStr)
{
    std::stringstream stream;
    stream << std::hex << std::uppercase << std::setfill('0');
    for (int v : data)
    {
        stream << std::setw(2) << v;
    }
    hexStr = stream.str();
}

template <typename... T>
static void selAddSystemRecord(
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

    std::string journalMsg(message + " from " + path + ": " +
                           " RecordType=" + std::to_string(selSystemType) +
                           ", GeneratorID=" + std::to_string(genId) +
                           ", EventDir=" + std::to_string(assert) +
                           ", EventData=" + selDataStr);

    AddToLog.append(journalMsg,
                    "xyz.openbmc_project.Logging.Entry.Level.Informational",
                    std::map<std::string, std::string>(
                        {{"SENSOR_PATH", path},
                         {"GENERATOR_ID", std::to_string(genId)},
                         {"RECORD_TYPE", std::to_string(selSystemType)},
                         {"EVENT_DIR", std::to_string(assert)},
                         {"SENSOR_DATA", selDataStr}}));
    conn->call(AddToLog);
#else
    unsigned int recordId = getNewRecordId();
    sd_journal_send("MESSAGE=%s", message.c_str(), "PRIORITY=%i", selPriority,
                    "MESSAGE_ID=%s", selMessageId, "IPMI_SEL_RECORD_ID=%d",
                    recordId, "IPMI_SEL_RECORD_TYPE=%x", selSystemType,
                    "IPMI_SEL_GENERATOR_ID=%x", genId,
                    "IPMI_SEL_SENSOR_PATH=%s", path.c_str(),
                    "IPMI_SEL_EVENT_DIR=%x", assert, "IPMI_SEL_DATA=%s",
                    selDataStr.c_str(), std::forward<T>(metadata)..., NULL);
#endif
}

static void selAddOemRecord(
    [[maybe_unused]] std::shared_ptr<sdbusplus::asio::connection> conn,
    [[maybe_unused]] const std::string& message,
    const std::vector<uint8_t>& selData, const uint8_t& recordType)
{
    // A maximum of 13 bytes of SEL event data are allowed in an OEM record
    if (selData.size() > selOemDataMaxSize)
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
        message + ": " + " RecordType=" + std::to_string(recordType) +
        ", GeneratorID=" + std::to_string(0) +
        ", EventDir=" + std::to_string(0) + ", EventData=" + selDataStr);

    AddToLog.append(journalMsg,
                    "xyz.openbmc_project.Logging.Entry.Level.Informational",
                    std::map<std::string, std::string>(
                        {{"SENSOR_PATH", ""},
                         {"GENERATOR_ID", std::to_string(0)},
                         {"RECORD_TYPE", std::to_string(recordType)},
                         {"EVENT_DIR", std::to_string(0)},
                         {"SENSOR_DATA", selDataStr}}));
    conn->call(AddToLog);
#else
    unsigned int recordId = getNewRecordId();
    sd_journal_send("MESSAGE=%s", message.c_str(), "PRIORITY=%i", selPriority,
                    "MESSAGE_ID=%s", selMessageId, "IPMI_SEL_RECORD_ID=%d",
                    recordId, "IPMI_SEL_RECORD_TYPE=%x", recordType,
                    "IPMI_SEL_DATA=%s", selDataStr.c_str(), NULL);
#endif
}

int main(int, char*[])
{
    // setup connection to dbus
    boost::asio::io_context io;
    auto conn = std::make_shared<sdbusplus::asio::connection>(io);

    // IPMI SEL Object
    conn->request_name(ipmiSelObject);
    auto server = sdbusplus::asio::object_server(conn);

    // Add SEL Interface
    std::shared_ptr<sdbusplus::asio::dbus_interface> ifaceAddSel =
        server.add_interface(ipmiSelPath, ipmiSelAddInterface);

    // Add a new SEL entry
    ifaceAddSel->register_method(
        "IpmiSelAdd",
        [conn](const std::string& message, const std::string& path,
               const std::vector<uint8_t>& selData, const bool& assert,
               const uint16_t& genId) {
        return selAddSystemRecord(conn, message, path, selData, assert, genId);
    });
    // Add a new OEM SEL entry
    ifaceAddSel->register_method("IpmiSelAddOem",
                                 [conn](const std::string& message,
                                        const std::vector<uint8_t>& selData,
                                        const uint8_t& recordType) {
        return selAddOemRecord(conn, message, selData, recordType);
    });

#ifndef SEL_LOGGER_SEND_TO_LOGGING_SERVICE
    // Clear SEL entries
    ifaceAddSel->register_method("Clear", []() { clearSelLogFiles(); });
    // Delete a SEL entry
    ifaceAddSel->register_method("IpmiSelDelete", [](const uint16_t& recordId) {
        return selDeleteRecord(recordId);
    });
#endif
    ifaceAddSel->initialize();

#ifdef SEL_LOGGER_MONITOR_THRESHOLD_EVENTS
    sdbusplus::bus::match_t thresholdAssertMonitor =
        startThresholdAssertMonitor(conn);
#endif

#ifdef REDFISH_LOG_MONITOR_PULSE_EVENTS
    sdbusplus::bus::match_t pulseEventMonitor = startPulseEventMonitor(conn);
#endif

#ifdef SEL_LOGGER_MONITOR_WATCHDOG_EVENTS
    sdbusplus::bus::match_t watchdogEventMonitor =
        startWatchdogEventMonitor(conn);
#endif

#ifdef SEL_LOGGER_MONITOR_THRESHOLD_ALARM_EVENTS
    startThresholdAlarmMonitor(conn);
#endif

#ifdef SEL_LOGGER_MONITOR_HOST_ERROR_EVENTS
    startHostErrorEventMonitor(conn);
#endif
    io.run();

    return 0;
}
