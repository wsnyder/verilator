#!/usr/bin/env python3
# DESCRIPTION: Verilator: Verilog Test driver/expect definition
#
# Copyright 2024 by Wilson Snyder. This program is free software; you
# can redistribute it and/or modify it under the terms of either the GNU
# Lesser General Public License Version 3 or the Perl Artistic License
# Version 2.0.
# SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

import vltest_bootstrap

test.scenarios('simulator')
test.top_filename = "t/t_gate_basic.v"
test.main_time_multiplier = 10e-7 / 10e-9

test.compile(timing_loop=True,
             verilator_flags2=["--timing --timescale 10ns/1ns -Wno-RISEFALLDLY -Wno-SPECIFYIGN"])

test.execute()

test.passes()
