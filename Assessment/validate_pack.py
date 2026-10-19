"""Validate source completeness, rubric coverage and safe submission wording.
No physical-test results are generated or assumed."""
import csv,json,re
from pathlib import Path
root=Path(__file__).resolve().parent
rows=list(csv.DictReader((root/'requirements.csv').open(encoding='utf-8')))
tests=json.loads((root/'test_cases.json').read_text(encoding='utf-8'))
ids=[r['Requirement Number'] for r in rows];tids=[t['id'] for t in tests]
assert len(ids)==len(set(ids)) and len(tids)==len(set(tids))
assert len(rows)>=22 and len(tests)>=10
minutes=sum(t['minutes'] for t in tests)
assert 10<=minutes<=20,f'rubric preferred 10-20 min; got {minutes}'
for r in rows:
    refs=[x.strip() for x in r['Test Cases'].split(',')]
    assert all(x in tids for x in refs),(r['Requirement Number'],refs)
    assert r['Implementation/evidence gate'].strip()
for t in tests:
    assert t['prerequisites'] and t['procedure'] and t['expected']
    assert set(t['requirements']).issubset(ids),t['id']
    # No internal white-box pass criteria should creep into external test plan.
    for line in t['expected']:
        assert not re.search(r'\b(debugger|ST-LINK console|UART log|task list|register view|xTaskGet)\b',line,re.I),(t['id'],line)
assert tests[-1]['id']=='TC13' and 'self-protection' in tests[-1]['name'].lower()
assert all('PASS' not in r['Implementation/evidence gate'] for r in rows)
print(f'PASS: {len(rows)} system requirements, {len(tests)} externally observable procedures, {minutes:.1f} min; no invented PASS claims')
