#!/usr/bin/env python3
"""Verify CubeIDE firmware dependencies and optional local recovery.
This is a source-tree check, NOT proof of a successful ARM build.
"""
from __future__ import annotations
import argparse
import hashlib
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DRIVER_FILES = (
    "STM32L4xx_HAL_Driver/Inc/stm32l4xx_hal_dfsdm.h",

    "STM32L4xx_HAL_Driver/Inc/stm32l4xx_hal.h",
    "STM32L4xx_HAL_Driver/Inc/stm32l4xx_hal_dma.h",
    "STM32L4xx_HAL_Driver/Inc/stm32l4xx_hal_i2c.h",
    "STM32L4xx_HAL_Driver/Inc/stm32l4xx_hal_i2c_ex.h",
    "CMSIS/Device/ST/STM32L4xx/Include/stm32l476xx.h",
    "CMSIS/Include/core_cm4.h",
)

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--verify-backup", action="store_true", help="also verify recovery copy hashes")
    ns = parser.parse_args()
    failures = []
    # The entire DFSDM HAL source/header are core-resident and restored together.
    for rel in ("Core/Inc/stm32l4xx_hal_dfsdm.h", "Core/Inc/g1_embedded_hal_dfsdm.inc", "Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_i2c.c", "Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_i2c_ex.c", "Recovery/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_i2c.c", "Recovery/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_i2c_ex.c",
                "Recovery/Core/Inc/stm32l4xx_hal_dfsdm.h", "Recovery/Core/Inc/g1_embedded_hal_dfsdm.inc"):
        if not (ROOT / rel).is_file(): failures.append(f"Missing DFSDM core component: {rel}")
    if (ROOT / "Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_dfsdm.c").exists():
        failures.append("Duplicate DFSDM implementation in Drivers/Src (only Core/Src may compile)")
    for rel in DRIVER_FILES:
        live = ROOT / "Drivers" / rel
        golden = ROOT / "Recovery/Drivers" / rel
        if not live.is_file():
            failures.append(f"Missing required HAL/CMSIS source: Drivers/{rel}")
        if not golden.is_file():
            failures.append(f"Missing packaged recovery source: Recovery/Drivers/{rel}")
        if ns.verify_backup and live.is_file() and golden.is_file():
            if hashlib.sha256(live.read_bytes()).digest() != hashlib.sha256(golden.read_bytes()).digest():
                failures.append(f"Driver differs from packaged recovery: Drivers/{rel}")
    config = ROOT / "Core/Inc/stm32l4xx_hal_conf.h"
    if not config.is_file() or "#define HAL_DFSDM_MODULE_ENABLED" not in config.read_text(errors="replace"):
        failures.append("HAL_DFSDM_MODULE_ENABLED missing from active Core/Inc/stm32l4xx_hal_conf.h")
    if config.is_file():
        conf = config.read_text(errors="replace")
        if not __import__("re").search(r"(?m)^\s*#\s*define\s+HAL_I2C_MODULE_ENABLED\b", conf):
            failures.append("HAL_I2C_MODULE_ENABLED missing from active HAL configuration")
        if '#include "stm32l4xx_hal_i2c.h"' not in conf:
            failures.append("HAL configuration does not include the I2C peripheral header")
    for rel in ("Core/Src/g1_product_board.c", "Recovery/Core/Src/g1_product_board.c"):
        board = ROOT / rel
        if not board.is_file():
            failures.append("Missing board implementation: " + rel)
            continue
        body = board.read_text(errors="replace")
        for needed in ('#include "stm32l4xx_hal_i2c.h"',
                       '#include "stm32l4xx_hal_i2c_ex.h"',
                       'OutputClock.Activation=ENABLE'):
            if needed not in body:
                failures.append(f"{rel}: missing required I2C/PDM dependency {needed}")
    if failures:
        for problem in failures: print("FAIL:", problem)
        print("Restore drivers: PREPARE_PROJECT.bat, then Project > Clean in STM32CubeIDE")
        return 2
    print("PASS: DFSDM HAL header AND implementation, CMSIS, HAL configuration and recovery bundle")
    print("NOTE: this is not an ARM compile/link result or physical hardware verification")
    return 0

if __name__ == "__main__": sys.exit(main())
