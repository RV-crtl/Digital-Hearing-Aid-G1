#!/usr/bin/env python3
"""Gate evidence matrix against invalid labels or missing source references."""
from pathlib import Path
import csv
root=Path(__file__).resolve().parents[2]
path=root/'Documentation'/'REQUIREMENTS_EVIDENCE.csv'
with path.open(newline='',encoding='utf-8') as f:
    rows=list(csv.DictReader(f))
assert len(rows)>=15
assert len({r['requirement_id'] for r in rows})==len(rows)
accepted={'host-verified','partially-verified','pending-evidence','implemented','not-implemented','not-integrated','not-verified'}
for r in rows:
    assert r['software_status'] in accepted, r
    assert r['blocking_action'].strip(),r
    impl=r['implementation']
    if impl not in {'unimplemented'} and not (root/impl).exists():
        raise AssertionError(f'Missing stated implementation/evidence path: {impl}')
    if not (root/r['evidence']).exists():
        raise AssertionError(f'Missing evidence plan: {r["evidence"]}')
print(f'EVIDENCE MATRIX: {len(rows)} traceable requirements; all hardware PASS claims deliberately deferred')
