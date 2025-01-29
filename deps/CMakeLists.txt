include(${CMAKE_CURRENT_LIST_DIR}/cmrc/CMakeRC.cmake)

##########################################

add_subdirectory(glm)
add_subdirectory(glfw)
add_subdirectory(crn)

##########################################

if(NOT MSVC)
  option(GEN_FILES "" ON)
  option(ENABLE_TESTING "" OFF)
  option(ENABLE_PROGRAMS "" OFF)
  add_subdirectory(mbedtls)
endif()

##########################################

option(glew-cmake_BUILD_SHARED "" OFF)
add_subdirectory(glew-cmake)

##########################################

option(HTTP_ONLY "" ON)
option(BUILD_CURL_EXE "" OFF)
option(BUILD_SHARED_LIBS "" OFF)
option(BUILD_STATIC_LIBS "" ON)
option(BUILD_EXAMPLES "" OFF)
option(BUILD_TESTING "" OFF)

option(CURL_ENABLE_EXPORT_TARGET "" OFF)

option(CURL_DISABLE_DICT "" ON)
option(CURL_DISABLE_FILE "" ON)
option(CURL_DISABLE_LDAP "" ON)
option(CURL_DISABLE_LDAPS "" ON)
option(CURL_DISABLE_FTP "" ON)
option(CURL_DISABLE_GOPHER "" ON)
option(CURL_DISABLE_IMAP "" ON)
option(CURL_DISABLE_MQTT "" ON)
option(CURL_DISABLE_POP3 "" ON)
option(CURL_DISABLE_RTSP "" ON)
option(CURL_DISABLE_SMTP "" ON)
option(CURL_DISABLE_SMB "" ON)
option(CURL_DISABLE_TELNET "" ON)
option(CURL_DISABLE_TFTP "" ON)

option(CURL_USE_LIBPSL "" OFF)
option(CURL_USE_LIBSSH2 "" OFF)
option(USE_NGHTTP2 "" OFF)
option(USE_LIBIDN2 "" OFF)

set(CURL_ZLIB "OFF" CACHE STRING "")
set(CURL_ZSTD "OFF" CACHE STRING "")
set(CURL_BROTLI "OFF" CACHE STRING "")

if(MSVC)
  set(CURL_USE_SCHANNEL ON)
else()
  set(CURL_USE_OPENSSL OFF)
  set(SSL_ENABLED ON)
  set(USE_MBEDTLS ON)
  list(APPEND CURL_LIBS ${MBEDTLS_LIBRARIES})

  get_target_property(MBEDTLS_INCLUDE_DIRS mbedtls INCLUDE_DIRECTORIES)
  include_directories(${MBEDTLS_INCLUDE_DIRS})
endif()

add_subdirectory(curl)

if(NOT MSVC)
  target_link_libraries(libcurl_static PRIVATE
    mbedtls
  )
endif()

##########################################

if(${MOMO_ENABLE_SANITIZER})
  option(protobuf_MSVC_STATIC_RUNTIME "" OFF)
else()
  option(protobuf_MSVC_STATIC_RUNTIME "" ON)
endif()

option(protobuf_BUILD_TESTS "" OFF)
option(protobuf_BUILD_PROTOBUF_BINARIES "" ON)
option(protobuf_BUILD_SHARED_LIBS "" OFF)
option(ABSL_PROPAGATE_CXX_STD "" ON)
add_subdirectory(protobuf)

##########################################

option(FT_DISABLE_ZLIB "" ON)
option(FT_DISABLE_BZIP2 "" ON)
option(FT_DISABLE_PNG "" ON)
option(FT_DISABLE_HARFBUZZ "" ON)
option(FT_DISABLE_BROTLI "" ON)

add_subdirectory(freetype)

##########################################

add_library(stb INTERFACE)
target_include_directories(stb INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/stb"
)

##########################################

add_library(xxHash
  "${CMAKE_CURRENT_LIST_DIR}/xxHash/xxhash.c"
)
target_include_directories(xxHash INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/xxHash"
)

##########################################

set(DOUBLE_PRECISION ON)
set(GENERATE_DEBUG_SYMBOLS ON)
set(CROSS_PLATFORM_DETERMINISTIC OFF)
set(INTERPROCEDURAL_OPTIMIZATION OFF)
set(FLOATING_POINT_EXCEPTIONS_ENABLED OFF)

set(OBJECT_LAYER_BITS 16)

set(USE_SSE4_1 ON)
set(USE_SSE4_2 ON)
set(USE_AVX OFF)
set(USE_AVX2 OFF)
set(USE_AVX512 OFF)
set(USE_LZCNT ON)
set(USE_TZCNT ON)
set(USE_F16C ON)
set(USE_FMADD ON)

option(CPP_RTTI_ENABLED "" ON)

add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/JoltPhysics/Build")

##########################################

add_subdirectory(libtommath)

if (MSVC)
    target_compile_options(libtommath PRIVATE "/wd4146")
endif()

##########################################

option(WITH_LTM "" OFF)

add_subdirectory(libtomcrypt)

target_compile_definitions(libtomcrypt PUBLIC
  USE_LTM
  LTM_DESC
  LTC_NO_PROTOTYPES
  LTC_NO_AES_NI
  LTC_NOTHING
  LTC_RIJNDAEL
  LTC_CBC_MODE
  LTC_CTR_MODE
  LTC_SHA1
  LTC_SHA512
  LTC_SHA256
  LTC_SPRNG
  LTC_YARROW
  LTC_FORTUNA
  LTC_MECC
  LTC_HMAC
  LTC_DER
  LTC_PKCS_5
  LTC_PKCS_12
  LTC_RNG_GET_BYTES
  LTC_RNG_MAKE_PRNG
  LTC_HASH_HELPERS
)

target_link_libraries(libtomcrypt PUBLIC libtommath)

if (MSVC)
    target_compile_options(libtomcrypt PRIVATE "/wd4703")
endif()
