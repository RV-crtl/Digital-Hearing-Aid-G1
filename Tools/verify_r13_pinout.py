#!/usr/bin/env python3
"""Verify declared CubeMX pinout, U13 PCB nets and existing runtime startup.
Static metadata audit: does not guarantee CubeMX code generation or real hardware.
"""
from pathlib import Path
import csv, sys
R=Path(__file__).resolve().parents[1]
name="G1_Product_R26_Adaptive_Audio_CubeIDE119"
props={}
for line in (R/(name+".ioc")).read_text().splitlines():
    if "=" in line and not line.startswith("#"):
        k,v=line.split("=",1)
        if k in props: raise AssertionError("Duplicate IOC key: "+k)
        props[k]=v
pins=[props[f"Mcu.Pin{i}"] for i in range(int(props["Mcu.PinsNb"]))]
assert len(pins)==len(set(pins)),"Duplicate IOC pin"
need={
  "PC2":"S_CKOUTDFSDM1", "PB14":"S_DATAIN2DFSDM1",
  "PB6":"I2C1_SCL", "PB7":"I2C1_SDA",
  "PA0":"GPXTI0", "PA1":"GPXTI1",
  "PA9":"USART1_TX", "PA10":"USART1_RX",
  "PA2":"USART2_TX", "PA3":"USART2_RX",
  "PA15 (JTDI)":"SAI2_FS_B", "PC10":"SAI2_SCK_B", "PC12":"SAI2_SD_B",
  "PC3":"SAI1_SD_A", "PB8":"SAI1_MCLK_A", "PB9":"SAI1_FS_A",
  "PB10":"SAI1_SCK_A", "PB12":"SAI2_FS_A", "PB13":"SAI2_SCK_A", "PB15":"SAI2_SD_A"
}
for pin,signal in need.items():
    assert pin in pins,f"{pin} missing from IOC pin list"
    key=pin.replace(" ",r"\ ")+".Signal" if " " in pin else pin+".Signal"
    assert props.get(key)==signal,f"{pin}: {props.get(key)} != {signal}"
for ip in ["I2C1","DFSDM1","DMA","SAI1","SAI2","USART1","USART2","FREERTOS"]:
    assert ip in [props.get(f"Mcu.IP{i}") for i in range(int(props["Mcu.IPNb"]))],ip
# DFSDM and SAI2B DMA ownership intentionally stays in authored C for CubeIDE 1.19.
# Adding CubeMX-owned DMA objects caused the R13 compiler/linker failures.
assert props["Dma.RequestsNb"]=="2"
assert props["DFSDM1.Fosr_FIL0"]=="40"
assert props["I2C1.Timing"]=="0x10D19CE4"
board=(R/"Core/Src/g1_product_board.c").read_text()
speaker=(R/"Core/Src/speaker_output.c").read_text()
uart=(R/"Core/Src/usart.c").read_text()
for s in ["GPIO_PIN_6|GPIO_PIN_7", "GPIO_PIN_0|GPIO_PIN_1", "DFSDM1_Channel2", "DMA1_Channel4", "GPIO_AF6_DFSDM1"]:
    assert s in board,s
for s in ["SAI2_Block_B", "GPIO_PIN_10 | GPIO_PIN_12", "DMA1_Channel7"]:
    assert s in speaker,s
for s in ["GPIO_PIN_9 | GPIO_PIN_10", "GPIO_AF7_USART1"]:
    assert s in uart,s
# Check against user-supplied exact PCB U13 pin labels rather than assuming pin number equals pin name.
rows=list(csv.DictReader((R/"Hardware/STM32_U13_PINMAP.csv").open()))
net={row["stm32_pin"]:row["pcb_net"] for row in rows}
for key in ["DFSDM1_CKOUT_10", "DFSDM1_DATIN2_35", "I2C1_SCL_58", "I2C1_SDA_59",
            "PA0_14", "PA1_15", "USART1_TX_42", "USART1_RX_43", "SAI2_FS_B_50", "SAI2_SCK_B_51", "SAI2_SD_B_53"]:
    assert key in net,key
print("PASS: 20 audio/UART/touch/encoder signals, manually owned DMA, PCB/U13 net references, HAL runtime mapping")
print("NOTE: CubeMX .ioc is hardware metadata. Existing Core/Src peripheral drivers are the firmware authority.")
print("NOTE: Physical I2C ACK, encoder signals, Bluetooth UART and audio DMA still require board measurements.")
