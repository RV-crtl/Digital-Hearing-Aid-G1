from pathlib import Path
r=Path(__file__).resolve().parents[1]
checks={
 "cubeide project":(r/".project").is_file() and "G1_Product_R26_Adaptive_Audio_CubeIDE119" in (r/".project").read_text(),
 "cubeide configuration":(r/".cproject").is_file() and "G1_Product_R26_Adaptive_Audio_CubeIDE119" in (r/".cproject").read_text(),
 "mic hal reset": "HAL_DFSDM_ChannelDeInit" in (r/"Core/Src/g1_product_board.c").read_text(),
 "mic independent path": "G1_MicTakeBlock(g_mono)" in (r/"Core/Src/demo_app.c").read_text(),
 "mic pclk": "RCC_DFSDM1CLKSOURCE_PCLK" in (r/"Core/Src/g1_product_board.c").read_text(),
 "Anna electrode thresholds": "(e>=8U)?42U:60U" in (r/"Core/Src/g1_product_board.c").read_text(),
 "encoder full cycle": "transitions_per_step=4U" in (r/"Core/Src/g1_product_board.c").read_text(),
 "May microphone highpass": "s_mic_hp_state" in (r/"Core/Src/g1_product_board.c").read_text(),
 "serial system checks": "SYSTEM CHECK" in (r/"Core/Src/demo_app.c").read_text(),
 "BT boot init before pairing": "PROFILE_THEN_DISCOVERABILITY" in (r/"Core/Src/demo_app.c").read_text(),
 "BT real UART counters": "bt_uart_rx_bytes" in (r/"Core/Src/demo_app.c").read_text(),
 "recovery board aligned": (r/"Core/Src/g1_product_board.c").read_bytes()==(r/"Recovery/Core/Src/g1_product_board.c").read_bytes(),
 "recovery app aligned": (r/"Core/Src/demo_app.c").read_bytes()==(r/"Recovery/Core/Src/demo_app.c").read_bytes(),
 "FreeRTOS task.h included": (r/"Middlewares/Third_Party/FreeRTOS/Source/include/task.h").is_file(),
 "Anna source reference": (r/"References/IndividualWorkingSources/Anna_CapTouch_Encoder/Core/Src/main.c").is_file(),
 "touch re-probe command": "TOUCH RETRY" in (r/"Core/Src/demo_app.c").read_text() and "G1_TouchReprobe" in (r/"Core/Src/g1_product_board.c").read_text(),
 "May25 source reference": (r/"References/IndividualWorkingSources/May25_Microphone_DFSDM/Core/Src/main.c").is_file(),
}
for k,v in checks.items(): print(("PASS" if v else "FAIL"),k)
assert all(checks.values()),"Integration checks failed"
print(f"{len(checks)}/{len(checks)} R12 structural checks passed (not hardware verification)")
