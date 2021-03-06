cmake_minimum_required (VERSION 3.6)
project (sel-logger CXX)
set (CMAKE_CXX_STANDARD 17)
set (CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable (sel-logger src/sel_logger.cpp)

option (
    SEL_LOGGER_MONITOR_THRESHOLD_EVENTS
    "Enable SEL Logger to monitor and automatically
     log SEL records for threshold sensor events"
    OFF
)

option (
    REDFISH_LOG_MONITOR_PULSE_EVENTS
    "Enable logger to monitor and automatically
     log events to Redfish for pulse type assert-deassert sensor events"
    OFF
)

option (
    SEL_LOGGER_MONITOR_WATCHDOG_EVENTS
    "Enable SEL Logger to monitor and automatically
     log SEL records for watchdog sensor events"
    OFF
)

option (
    SEL_LOGGER_SEND_TO_LOGGING_SERVICE
    "Make SEL Logger to send sel to logging service instead of journal"
    OFF
)

option (
    SEL_LOGGER_MONITOR_THRESHOLD_ALARM_EVENTS
    "Enable SEL Logger to monitor threshold alarm signals and
     automatically log SEL records for threshold sensor events"
    OFF
)

target_compile_definitions (
    sel-logger PRIVATE
    $<$<BOOL:${SEL_LOGGER_MONITOR_THRESHOLD_EVENTS}>: -DSEL_LOGGER_MONITOR_THRESHOLD_EVENTS>
    $<$<BOOL:${REDFISH_LOG_MONITOR_PULSE_EVENTS}>: -DREDFISH_LOG_MONITOR_PULSE_EVENTS>
    $<$<BOOL:${SEL_LOGGER_MONITOR_WATCHDOG_EVENTS}>: -DSEL_LOGGER_MONITOR_WATCHDOG_EVENTS>
    $<$<BOOL:${SEL_LOGGER_SEND_TO_LOGGING_SERVICE}>: -DSEL_LOGGER_SEND_TO_LOGGING_SERVICE>
    $<$<BOOL:${SEL_LOGGER_MONITOR_THRESHOLD_ALARM_EVENTS}>: -DSEL_LOGGER_MONITOR_THRESHOLD_ALARM_EVENTS>
)

target_include_directories (sel-logger PRIVATE ${CMAKE_SOURCE_DIR})

set(LINK_LIBS systemd sdbusplus pthread phosphor_logging -lstdc++fs)

if (SEL_LOGGER_SEND_TO_LOGGING_SERVICE)
    list(APPEND LINK_LIBS phosphor_dbus)
endif()

target_link_libraries (sel-logger ${LINK_LIBS})

include_directories (${CMAKE_CURRENT_SOURCE_DIR}/include)

install (TARGETS sel-logger
         RUNTIME DESTINATION bin
         LIBRARY DESTINATION lib
         ARCHIVE DESTINATION lib/static)

find_package (Boost 1.66 REQUIRED)
include_directories (${BOOST_SRC_DIR})

add_definitions (-DBOOST_ERROR_CODE_HEADER_ONLY)
add_definitions (-DBOOST_SYSTEM_NO_DEPRECATED)
add_definitions (-DBOOST_ALL_NO_LIB)
add_definitions (-DBOOST_NO_RTTI)
add_definitions (-DBOOST_NO_TYPEID)

# Temporarily disable rtti
# See https://github.com/chriskohlhoff/asio/issues/533
#set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")
#set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-rtti")

set (
    SERVICE_FILES
    ${PROJECT_SOURCE_DIR}/service_files/xyz.openbmc_project.Logging.IPMI.service
)
install (FILES ${SERVICE_FILES} DESTINATION /lib/systemd/system/)
