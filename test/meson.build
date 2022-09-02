gtest = dependency('gtest', main: true, disabler: true, required: build_tests)
gmock = dependency('gmock', disabler: true, required: build_tests)
inc_dir = include_directories('../include')

cpp = meson.get_compiler('cpp')
boost_coroutine = cpp.find_library('boost_coroutine', required: true)

sensorutils_test = executable(
  'test_sensorutils',
  'test_sensorutils.cpp',
  include_directories: [inc_dir],
  dependencies: [
    gtest,
    gmock,
  ])

test('test_sensorutils', sensorutils_test, workdir: meson.current_source_dir())
