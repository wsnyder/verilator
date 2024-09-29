// DESCRIPTION: Verilator: Verilog Test module
//
// This file ONLY is placed under the Creative Commons Public Domain, for
// any use, without warranty, 2022 by Wilson Snyder.
// SPDX-License-Identifier: CC0-1.0

module t(
   clk
   /*AUTOARG*/);
   input clk;
   wor [7:0]p1;
   trior [7:0]p2;
   wand [7:0]p3;
   triand [7:0]p4;
   wire [7:0]z1;
   wire [7:0]z2;
   logic [7:0]x;
   logic [7:0]y;
   assign p1 = x & y;
   assign p1 = x | y;
   assign p2 = x & y;
   assign p2 = x | y;
   assign p3 = x & y;
   assign p3 = x | y;
   assign p4 = x & y;
   assign p4 = x | y;
   assign z1 = (x & y) | (x | y);
   assign z2 = (x & y) & (x | y);
   integer cyc = 0;
   always @ (posedge clk) begin
      cyc <= cyc + 1;
      x = {$random}[7:0];
      y = {$random}[7:0];
      if ((p1 != z1) || (p2 != z1)) begin
         $display("Wor Error!, wor assign result is %h and %h, the result should be %h", p1, p2, z1);
         $stop;
      end
      if ((p3 != z2) || (p4 != z2)) begin
         $display("Wand Error!, wand assign result is %h and %h, the result should be %h", p3, p4, z2);
         $stop;
      end
      if (cyc == 20) begin
         $write("*-* All Finished *-*\n");
         $finish;
      end
   end
endmodule
