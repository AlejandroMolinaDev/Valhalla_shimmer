# Reads the project version from the top-level VERSION file so that CMake,
# the plugin binaries and CI artefacts all share a single source of truth.
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/../VERSION" AB_VERSION LIMIT_COUNT 1)
string(STRIP "${AB_VERSION}" AB_VERSION)

if(NOT AB_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "VERSION file must contain MAJOR.MINOR.PATCH, got '${AB_VERSION}'")
endif()
