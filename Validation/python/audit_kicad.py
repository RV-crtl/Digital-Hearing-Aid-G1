#!/usr/bin/env python3
"""Non-destructive KiCad source-completeness audit. Does NOT run ERC or DRC."""
from __future__ import annotations
from pathlib import Path
import re, json, sys
ROOT=Path(__file__).resolve().parents[2]
K=ROOT/'Hardware'/'Original_KiCad'
def check():
    project=K/'260510-Digital_hearing_aid.kicad_pro'
    pcb=K/'260510-Digital_hearing_aid.kicad_pcb'
    sheets=list(K.glob('*.kicad_sch'))
    if not project.is_file() or not pcb.is_file() or len(sheets)<10:
        raise SystemExit('FAIL: missing KiCad project, board or hierarchical schematic sheets')
    missing=[]; found=[]
    for name in ('fp-lib-table','sym-lib-table'):
        table=K/name
        if not table.is_file():raise SystemExit(f'FAIL: missing {name}')
        for target in re.findall(r'\(uri\s+"([^"]+)"\)',table.read_text(encoding='utf-8')):
            # Fail safely on non-portable absolute paths rather than pretending
            # they can be opened from another team member's computer.
            resolved=target.replace('${KIPRJMOD}',str(K))
            if resolved.startswith(('C:', 'D:', '/home/','/Users/')):
                missing.append(target)
            elif Path(resolved).exists():found.append(target)
            else:missing.append(target)
    row_count=sum(1 for _ in (ROOT/'Hardware'/'STM32_U13_PINMAP.csv').open(encoding='utf-8'))-1
    board=pcb.read_text(encoding='utf-8',errors='replace')
    report={'project_file':str(project.relative_to(ROOT)),'schematic_sheets':len(sheets),
       'pcb_file':str(pcb.relative_to(ROOT)),'pcb_footprint_instances':len(re.findall(r'\(footprint\s',board)),
       'pcb_segment_objects':len(re.findall(r'\(segment\s',board)),
       'stm32_pad_map_rows':row_count,'library_paths_found':len(found),
       'library_paths_unresolved':len(missing),'unresolved_library_uris':missing,
       'erc':'NOT_RUN','drc':'NOT_RUN','fabrication_readiness':'NOT_ESTABLISHED'}
    outfile=ROOT/'Validation'/'results'/'kicad_audit.json';outfile.parent.mkdir(parents=True,exist_ok=True)
    outfile.write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(f"PASS: KiCad project {len(sheets)} sheets, {report['pcb_footprint_instances']} footprints, {report['pcb_segment_objects']} PCB track segments, {row_count} MCU pads")
    if missing:print(f'WARN: {len(missing)} external symbol/footprint libraries unresolved; see Hardware/README.md')
    print('PENDING: Electrical Rules Check, Design Rules Check, fabrication, assembled board bring-up')
    return report
if __name__=='__main__':check()
