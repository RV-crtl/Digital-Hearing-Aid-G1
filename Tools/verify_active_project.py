#!/usr/bin/env python3
"""Detect stale CubeIDE project imports and missing local DFSDM files."""
from pathlib import Path
import sys, xml.etree.ElementTree as ET
root=Path(__file__).resolve().parents[1]
expected="261104-Digital_Hearing_Aid"
problems=[]
try: actual=ET.parse(root/".project").getroot().findtext("name")
except Exception as exc: actual=f"parse error: {exc}"
print("Physical source location:", root)
print("Eclipse project name:", actual)
if actual!=expected: problems.append("Eclipse project name is not the 261104 project")
for relative in (".cproject",expected+".ioc", "Core/Src/g1_product_board.c", "Core/Inc/stm32l4xx_hal_dfsdm.h", "Core/Inc/g1_embedded_hal_dfsdm.inc"):
    target=root/relative
    print(("FOUND" if target.is_file() else "MISSING"),target)
    if not target.is_file(): problems.append(relative)
conf=root/"Core/Inc/stm32l4xx_hal_conf.h"
if not conf.is_file() or '#define HAL_DFSDM_MODULE_ENABLED' not in conf.read_text():
    problems.append("DFSDM HAL configuration")
vendor_src=root/"Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_dfsdm.c"
if vendor_src.is_file(): problems.append("duplicate DFSDM HAL source under Drivers")
if problems:
    print("FAILED preflight:", ', '.join(problems));sys.exit(2)
ioc = root / (expected+".ioc")
if ioc.is_file():
    ioc_text = ioc.read_text(errors="replace")
    for token in ("ProjectManager.ProjectFileName="+expected+".ioc", "ProjectManager.ProjectName="+expected):
        if token not in ioc_text: problems.append("IOC metadata mismatch: "+token)
# A mistaken name breaks the headless import and produces an older firmware build.
build_script = (root/"G1_BUILD_ARM.ps1")
if build_script.is_file() and '$Name = "'+expected+'"' not in build_script.read_text():
    problems.append("Headless ARM build script points to a different project")
if problems:
    print("FAILED metadata checks:", ', '.join(problems));sys.exit(2)
print("PASS: Correct 261104 project and complete DFSDM compilation units")
print("In CubeIDE Project Explorer, the project NAME MUST MATCH:",expected)
