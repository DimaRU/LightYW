cmake_minimum_required(VERSION 3.10)

message(IDF_VERSION ": $ENV{IDF_VERSION}; idf version number: ${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}.${IDF_VERSION_PATCH}")
get_filename_component(SDKCONFIG_COMMON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/sdkconfig.defaults" ABSOLUTE)

# Sequence of loading default sdkconfig files are based on
# https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/build-system.html#custom-sdkconfig-defaults
#
# First default file should be sdkconfig.defaults,
# if that is present then sdkconfig.defaults.target will be auto loaded
set(SDKCONFIG_FILES "sdkconfig.defaults")

# Append the configurations externally provided using `SDKCONFIG_DEFAULTS` variable
list(APPEND SDKCONFIG_FILES ${SDKCONFIG_DEFAULTS})

# setting the final sdkconfig files list to SDKCONFIG_DEFAULTS
set(SDKCONFIG_DEFAULTS ${SDKCONFIG_FILES})

message("Sequence of SDKCONFIG files: ${SDKCONFIG_DEFAULTS}")
