# LLVM toolchain for Windows (native): clang-cl with lld-link, llvm-lib, llvm-rc, llvm-mt (as Esia's own
# cmake/toolchains/clang-cl-windows.cmake). Run from a "x64 Native Tools" prompt (or after vcvars64.bat) so the MSVC STL /
# CRT and the Windows SDK are on INCLUDE / LIB, with LLVM's bin directory on PATH; build.bat does both.
set(CMAKE_C_COMPILER   clang-cl)
set(CMAKE_CXX_COMPILER clang-cl)
set(CMAKE_LINKER       lld-link)
set(CMAKE_AR           llvm-lib)
set(CMAKE_RC_COMPILER  llvm-rc)
set(CMAKE_MT           llvm-mt)
