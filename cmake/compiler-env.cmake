include_guard()

##########################################

cmake_policy(SET CMP0069 NEW) 
set(CMAKE_POLICY_DEFAULT_CMP0069 NEW)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
#set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)

##########################################

if(MSVC)
  momo_add_c_and_cxx_compile_options(
    /sdl
    /GS
    /Gy
    /guard:cf
  )

  momo_add_compile_options(CXX
    /Zc:__cplusplus
  )

  add_link_options(
    /INCREMENTAL:NO
  )

  momo_add_c_and_cxx_release_compile_options(
    /Ob2
    #/GL
  )

  momo_add_release_link_options(
    #/LTCG
  )
endif()
##########################################

if(MOMO_ENABLE_SANITIZER)
momo_add_c_and_cxx_compile_options(
  -fsanitize=address
)
endif()

##########################################

if(MOMO_ENABLE_SANITIZER)
  # ASAN on Windows needs /MD
  # https://developercommunity.visualstudio.com/t/c-address-sanitizer-statically-linked-dlls-do-not/1403680
  set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded$<$<CONFIG:Debug>:Debug>DLL)
else()
  set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded$<$<CONFIG:Debug>:Debug>)
endif()

##########################################

if(MSVC)
  add_link_options(
    $<$<NOT:$<STREQUAL:${CMAKE_MSVC_RUNTIME_LIBRARY},MultiThreaded>>:/NODEFAULTLIB:libcmt.lib>
    $<$<NOT:$<STREQUAL:${CMAKE_MSVC_RUNTIME_LIBRARY},MultiThreadedDLL>>:/NODEFAULTLIB:msvcrt.lib>
    $<$<NOT:$<STREQUAL:${CMAKE_MSVC_RUNTIME_LIBRARY},MultiThreadedDebug>>:/NODEFAULTLIB:libcmtd.lib>
    $<$<NOT:$<STREQUAL:${CMAKE_MSVC_RUNTIME_LIBRARY},MultiThreadedDebugDLL>>:/NODEFAULTLIB:msvcrtd.lib>
  )
endif()

##########################################

set(OPT_DEBUG "-O0 -g")
set(OPT_RELEASE "-O3 -g")

if(MSVC)
  set(OPT_DEBUG "/Od /Ob0 /Zi")
  set(OPT_RELEASE "/O2 /Ob2 /Zi")

  add_link_options(/DEBUG)
endif()

set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} ${OPT_DEBUG}")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} ${OPT_DEBUG}")

set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} ${OPT_RELEASE}")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} ${OPT_RELEASE}")

##########################################

if(CMAKE_GENERATOR MATCHES "Visual Studio")
  momo_add_c_and_cxx_compile_options(/MP)
endif()