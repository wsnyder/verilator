# DESCRIPTION: Verilator: CI Windows Power Shell - Verilate a test
#
# Copyright 2024 by Wilson Snyder. This program is free software; you
# can redistribute it and/or modify it under the terms of either the GNU
# Lesser General Public License Version 3 or the Perl Artistic License
# Version 2.0.
#
# SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0
################################################################################


Set-PSDebug -Trace 1

cd install
$Env:VERILATOR_ROOT=$PWD
cd examples/cmake_tracing_c
mkdir build
cd build
cmake ..
cmake --build . --config Release -j 3

Release/example.exe --debugi 9

#FIXME cd ..
#FIXME dir
#FIXME Remove-Item -path build -recurse
#

#  nano D:\a\verilator\verilator\repo\install\examples\cmake_hello_c\build\CMakeFiles\example.dir\Vtop.dir\Vtop__main.cpp
#  cmake --build . --config Release -j 3 ; Release/example.exe


#exit 0
