cmake_minimum_required(VERSION 3.5...3.30)
project(AnimepaheCLI VERSION 0.3.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CPR_USE_SYSTEM_CURL ON)

# Configure Abseil options before making it available
set(ABSL_PROPAGATE_CXX_STD ON)
set(ABSL_ENABLE_INSTALL ON)

include(FetchContent)

# Fetch Abseil first (required by RE2)
fetchcontent_declare(
  absl
  GIT_REPOSITORY https://github.com/abseil/abseil-cpp.git
  GIT_TAG 20240116.2 # Use a stable release tag
)

# Fetch RE2
fetchcontent_declare(
  re2
  GIT_REPOSITORY https://github.com/google/re2.git
  GIT_TAG 2024-04-01 # Use a stable release tag
)

# FMT
fetchcontent_declare(
  fmt
  GIT_REPOSITORY https://github.com/fmtlib/fmt.git
  GIT_TAG e69e5f977d458f2650bb346dadf2ad30c5320281 # v10.2.1
)

# CPR
fetchcontent_declare(
  cpr
  GIT_REPOSITORY https://github.com/libcpr/cpr.git
  GIT_TAG 1.10.5
)

# cxxopts
fetchcontent_declare(
  cxxopts
  GIT_REPOSITORY https://github.com/jarro2783/cxxopts.git
  GIT_TAG v3.1.1
)

fetchcontent_declare(
  pugixml
  GIT_REPOSITORY https://github.com/zeux/pugixml.git
  GIT_TAG v1.15 # Or a specific release tag like v1.13
)

fetchcontent_declare(
  json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.12.0
)

# Fetch kuba--/zip (minimal dependencies, simple API)
fetchcontent_declare(
  zip
  GIT_REPOSITORY https://github.com/kuba--/zip.git
  GIT_TAG v0.3.5 # Use stable version
)

fetchcontent_makeavailable(absl re2 cxxopts fmt cpr pugixml json zip)

# SQLite amalgamation — used on Windows to read Chromium-based browser cookie stores
if(WIN32)
  fetchcontent_declare(
    sqlite_amalgamation
    URL https://www.sqlite.org/2024/sqlite-amalgamation-3470000.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  fetchcontent_makeavailable(sqlite_amalgamation)
  add_library(sqlite3_static STATIC ${sqlite_amalgamation_SOURCE_DIR}/sqlite3.c)
  target_include_directories(sqlite3_static PUBLIC ${sqlite_amalgamation_SOURCE_DIR})
  target_compile_definitions(sqlite3_static PUBLIC
    SQLITE_OMIT_LOAD_EXTENSION
    SQLITE_THREADSAFE=0
  )
endif()

set(SRC_FILES
  main.cpp
  libs/utils.cpp
  libs/animepahe.cpp
  libs/kwikpahe.cpp
  libs/streamkey.cpp
  libs/downloader.cpp
  libs/ziputils.cpp
)

# Include Windows-only files
if(WIN32)
  list(APPEND SRC_FILES resource.rc)
  list(APPEND SRC_FILES libs/githubupdater.cpp)
  list(APPEND SRC_FILES libs/cookiejar.cpp)
endif()

add_executable(animepahe-cli-beta ${SRC_FILES})

target_include_directories(animepahe-cli-beta
  PRIVATE
  ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(animepahe-cli-beta
  PRIVATE
  pugixml
  zip
  cpr::cpr
  fmt::fmt
  re2::re2
  cxxopts::cxxopts
  nlohmann_json::nlohmann_json
)

if(WIN32)
  target_link_libraries(animepahe-cli-beta
    PRIVATE
    sqlite3_static
    crypt32
    bcrypt
  )
endif()
