cmake_minimum_required(VERSION 3.20)
project(FarCry4_FPSFix LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "FarCry4_FPSFix is a Windows-only program and must be built on Windows.")
endif()

add_executable(FarCry4_FPSFix src/FarCry4_FPSFix.cpp)
target_compile_features(FarCry4_FPSFix PRIVATE cxx_std_17)
target_compile_definitions(FarCry4_FPSFix PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(FarCry4_FPSFix PRIVATE winmm shell32 ole32 psapi advapi32)
if(MSVC)
    target_compile_options(FarCry4_FPSFix PRIVATE /W4 /permissive- /EHsc)
endif()
