project(
  'phosphor-sel-logger',
  'cpp',
  version: '0.1',
  meson_version: '>=0.57.0',
  default_options: [
    'warning_level=3',
    'cpp_std=c++20',
  ])

cpp_args = []

deps = [
  dependency('sdbusplus', fallback: ['sdbusplus', 'sdbusplus_dep']),
]

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
if get_option('send-to-logger')
  cpp_args += '-DSEL_LOGGER_SEND_TO_LOGGING_SERVICE'

  deps += dependency(
   'phosphor-logging',
   fallback: ['phosphor-logging', 'phosphor_logging_dep'])
endif

executable(
  'sel-logger',
  'src/sel_logger.cpp',
  include_directories: include_directories('include'),
  implicit_include_directories: false,
  cpp_args: cpp_args,
  dependencies: deps,
  install: true,
  install_dir: get_option('bindir'))

systemd = dependency('systemd')
if systemd.found()
  install_data(
    'service_files/xyz.openbmc_project.Logging.IPMI.service',
    install_dir: systemd.get_variable(pkgconfig: 'systemdsystemunitdir'))
endif
