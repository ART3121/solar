`timescale 1ns/1ps
module counter_tb;
    reg clk = 1'b0;
    reg reset = 1'b1;
    wire [`DATA_WIDTH-1:0] value;

    counter dut (.clk(clk), .reset(reset), .value(value));

    always #5 clk = ~clk;

    initial begin
        $dumpfile("basic.vcd");
        $dumpvars(0, counter_tb);
        #12 reset = 1'b0;
        #100;
        $display("counter value: %0d", value);
        $finish;
    end
endmodule
