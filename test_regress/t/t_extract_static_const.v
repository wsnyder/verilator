// DESCRIPTION: Verilator: Verilog Test module
//
// This file ONLY is placed under the Creative Commons Public Domain, for
// any use, without warranty, 2020 by Geza Lore.
// SPDX-License-Identifier: CC0-1.0

module t (/*AUTOARG*/);

   bit [255:0] C;
   initial C = {32'h1111_1111,
                           32'h2222_2222,
                           32'h3333_3333,
                           32'h4444_4444,
                           32'h5555_5555,
                           32'h6666_6666,
                           32'h7777_7777,
                           32'h8888_8888};

   // Same values as above, but with different type
   logic [255:0] D;
   initial D = {32'h1111_1111,
                           32'h2222_2222,
                           32'h3333_3333,
                           32'h4444_4444,
                           32'h5555_5555,
                           32'h6666_6666,
                           32'h7777_7777,
                           32'h8888_8888};

   int  i;

   initial begin
      // Note: Base index via $c to prevent optimization by Verilator
      i = $c(0*32); $display("0x%8x", C[i+:32]);
      i = $c(1*32); $display("0x%8x", D[i+:32]);
      i = $c(2*32); $display("0x%8x", C[i+:32]);
      i = $c(3*32); $display("0x%8x", D[i+:32]);
      i = $c(4*32); $display("0x%8x", C[i+:32]);
      i = $c(5*32); $display("0x%8x", D[i+:32]);
      i = $c(6*32); $display("0x%8x", C[i+:32]);
      i = $c(7*32); $display("0x%8x", D[i+:32]);
      $write("*-* All Finished *-*\n");
      $finish;
   end

endmodule
