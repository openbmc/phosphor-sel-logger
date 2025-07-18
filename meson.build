project(
    'phosphor-sel-logger',
    'cpp',
    version: '0.1',
    meson_version: '>=1.1.1',
    default_options: ['werror=true', 'warning_level=3', 'cpp_std=c++23'],
)

cpp_args = []

deps = [dependency('sdbusplus'), dependency('libsystemd'), dependency('boost')]

if get_option('log-threshold')
    cpp_args += '-DSEL_LOGGER_MONITOR_THRESHOLD_EVENTS'
endif
if get_option('log-pulse')
    cpp_args += '-DREDFISH_LOG_MONITOR_PULSE_EVENTS'
endif
if get_option('log-watchdog')
    cpp_args += '-DSEL_LOGGER_MONITOR_WATCHDOG_EVENTS'
endif
if get_option('log-alarm')
    cpp_args += '-DSEL_LOGGER_MONITOR_THRESHOLD_ALARM_EVENTS'
endif
if get_option('log-host')
    cpp_args += '-DSEL_LOGGER_MONITOR_HOST_ERROR_EVENTS'
endif
if get_option('send-to-logger')
    cpp_args += '-DSEL_LOGGER_SEND_TO_LOGGING_SERVICE'

    deps += dependency('phosphor-logging')
endif
if get_option('sel-delete')
    cpp_args += '-DSEL_LOGGER_ENABLE_SEL_DELETE'

    deps += dependency('phosphor-dbus-interfaces')
endif

executable(
    'sel-logger',
    'src/sel_logger.cpp',
    include_directories: include_directories('include'),
    implicit_include_directories: false,
    cpp_args: cpp_args,
    dependencies: deps,
    install: true,
    install_dir: get_option('bindir'),
)

systemd = dependency('systemd')
if systemd.found()
    install_data(
        'service_files/xyz.openbmc_project.Logging.IPMI.service',
        install_dir: systemd.get_variable('systemd_system_unit_dir'),
    )
endif
