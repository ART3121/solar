`timescale 1ns/1ps
module counter_overflow_tb;
    reg clk = 1'b0;
    reg reset = 1'b1;
    wire [`DATA_WIDTH-1:0] value;
    integer cycle;

    counter dut (.clk(clk), .reset(reset), .value(value));

    always #5 clk = ~clk;

    initial begin
        $dumpfile("overflow.vcd");
        $dumpvars(0, counter_overflow_tb);
        #12 reset = 1'b0;
        for (cycle = 0; cycle < (1 << `DATA_WIDTH); cycle = cycle + 1)
            @(posedge clk);
        #1;
        if (value !== {`DATA_WIDTH{1'b0}}) begin
            $display("FAIL: expected wrap to zero, got %0d", value);
            $finish(1);
        end
        $display("PASS: counter wrapped after %0d cycles", cycle);
        $finish;
    end
endmodule
