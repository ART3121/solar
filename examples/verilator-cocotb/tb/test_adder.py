import cocotb
from cocotb.triggers import Timer


@cocotb.test()
async def adds_values(dut):
    dut.a.value = 9
    dut.b.value = 4
    await Timer(1, unit="ns")
    assert int(dut.sum.value) == 13
