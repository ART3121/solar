`include "counter_defs.vh"

module counter (
    input wire clk,
    input wire reset,
    output reg [`DATA_WIDTH-1:0] value
);
    always @(posedge clk) begin
        if (reset)
            value <= {`DATA_WIDTH{1'b0}};
        else
            value <= value + 1'b1;
    end
endmodule
