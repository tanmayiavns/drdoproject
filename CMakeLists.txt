cmake_minimum_required(VERSION 3.16)
project(MyChecker)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(LLVM REQUIRED CONFIG)
find_package(Clang REQUIRED CONFIG)

message(STATUS "LLVM version: ${LLVM_PACKAGE_VERSION}")

include_directories(
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)

add_definitions(${LLVM_DEFINITIONS})

add_executable(MyChecker MyCheck.cpp)

target_compile_options(MyChecker PRIVATE
    # Fix 1: VS 2022 Insiders STL requires Clang 20+, we have Clang 18 — bypass check
    -D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH

    # Fix 2: __builtin_verbose_trap is a Clang 20+ intrinsic not in Clang 18
    # The VS Insiders STL uses it in error macros — stub it out safely
    "-D__builtin_verbose_trap(a,b)=((void)0)"

    # Fix 3: suppress unrelated warnings
    -Wno-trigraphs
    -Wno-deprecated-declarations
    -Wno-unused-parameter
)

target_link_libraries(MyChecker
    clangTooling
    clangASTMatchers
    clangAST
    clangBasic
    clangFrontend
    clangLex
    clangDriver
    clangSerialization
    clangParse
    clangSema
    clangAnalysis
    clangEdit
    clangRewrite
    LLVMSupport
    LLVMCore
    LLVMOption
)