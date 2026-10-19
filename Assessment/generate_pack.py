#!/usr/bin/env python3
"""Reproducible PDF exports of ZEIT4232-style black-box requirements/test plan.

Outputs are DRAFTS until the group agrees system-level scope and all physical
acceptance thresholds. Uses external observable behaviour, not internal UART or
RTOS debugger counters. This is document generation, not hardware validation.

Usage: python3 Assessment/generate_pack.py --output Assessment/generated
Dependencies: Python 3.10+, reportlab 4.x
"""
import argparse
import csv
import json
from pathlib import Path
from xml.sax.saxutils import escape

from reportlab.lib import colors
from reportlab.lib.enums import TA_LEFT, TA_CENTER
from reportlab.lib.pagesizes import A4, A3, landscape
from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
from reportlab.lib.units import mm
from reportlab.platypus import SimpleDocTemplate, Paragraph, Table, TableStyle, Spacer, KeepTogether, PageBreak
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfgen import canvas

ROOT=Path(__file__).resolve().parent
NAVY=colors.HexColor('#0D223F')
TEAL=colors.HexColor('#087F8C')
PALE=colors.HexColor('#EDF6F8')
GRAY=colors.HexColor('#475569')
RED=colors.HexColor('#B42318')
BORDER=colors.HexColor('#CBD5E1')

FONT='Helvetica'
BOLD='Helvetica-Bold'
for label,candidate in [('DHA-Regular','/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'),('DHA-Bold','/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf')]:
    if Path(candidate).exists():
        pdfmetrics.registerFont(TTFont(label,candidate))
if 'DHA-Regular' in pdfmetrics.getRegisteredFontNames():
    FONT='DHA-Regular';BOLD='DHA-Bold'

def ptxt(x):
    # ReportLab para markup; preserve literal symbols safely.
    return escape(str(x)).replace('\n','<br/>')

def doc_styles():
    s=getSampleStyleSheet()
    s.add(ParagraphStyle(name='DTitle', fontName=BOLD,fontSize=18,leading=25,textColor=NAVY,spaceAfter=12))
    s.add(ParagraphStyle(name='DH1',fontName=BOLD,fontSize=12,leading=18,textColor=NAVY,spaceBefore=12,spaceAfter=7))
    s.add(ParagraphStyle(name='DH2',fontName=BOLD,fontSize=10,leading=15,textColor=TEAL,spaceBefore=8,spaceAfter=5))
    s.add(ParagraphStyle(name='DBody',fontName=FONT,fontSize=8,leading=12,spaceAfter=5))
    s.add(ParagraphStyle(name='DCell',fontName=FONT,fontSize=7.2,leading=11))
    s.add(ParagraphStyle(name='DHeader',fontName=BOLD,fontSize=7.4,leading=11,textColor=colors.white))
    s.add(ParagraphStyle(name='DCellBold',fontName=BOLD,fontSize=7.2,leading=11,textColor=NAVY))
    s.add(ParagraphStyle(name='DMuted',fontName=FONT,fontSize=7,leading=10,textColor=GRAY))
    return s

def paginate(canvas, doc):
    canvas.saveState()
    canvas.setStrokeColor(BORDER);canvas.line(16*mm,15*mm,194*mm,15*mm)
    canvas.setFont(FONT,7);canvas.setFillColor(GRAY)
    canvas.drawString(17*mm,10.5*mm,'GROUP DRAFT — Not a record of physical testing')
    canvas.drawRightString(193*mm,10.5*mm,f'Page {doc.page}')
    canvas.restoreState()

def make_table(data,widths,s):
    data=[[Paragraph(ptxt(c),s['DHeader'] if rowno==0 else s['DCell']) for c in row] for rowno,row in enumerate(data)]
    t=Table(data,colWidths=widths,repeatRows=1,hAlign='LEFT')
    t.setStyle(TableStyle([
        ('BACKGROUND',(0,0),(-1,0),NAVY),('TEXTCOLOR',(0,0),(-1,0),colors.white),
        ('BACKGROUND',(0,1),(-1,-1),colors.white),
        ('ROWBACKGROUNDS',(0,1),(-1,-1),[colors.white,PALE]),
        ('GRID',(0,0),(-1,-1),.3,BORDER),('VALIGN',(0,0),(-1,-1),'TOP'),
        ('LEFTPADDING',(0,0),(-1,-1),6),('RIGHTPADDING',(0,0),(-1,-1),6),
        ('TOPPADDING',(0,0),(-1,-1),6),('BOTTOMPADDING',(0,0),(-1,-1),6)]))
    return t

def make_assessment(out):
    rows=list(csv.DictReader((ROOT/'requirements.csv').open(encoding='utf-8')))
    tests=json.loads((ROOT/'test_cases.json').read_text(encoding='utf-8'))
    path=out/'SYSTEM_REQUIREMENTS_AND_BLACK_BOX_TEST_PLAN_DRAFT.pdf'
    s=doc_styles()
    doc=SimpleDocTemplate(str(path),pagesize=A4,rightMargin=16*mm,leftMargin=16*mm,topMargin=16*mm,bottomMargin=19*mm,
                          title='Digital Hearing Aid — Revised System Requirements and Black Box Test Plan (Draft)',author='Group project')
    story=[Paragraph('Digital Hearing Aid',s['DTitle']),Paragraph('Revised system requirements and black-box test plan',s['DH1'])]
    story.append(Paragraph('DRAFT — Group sign-off and actual hardware evidence required before submission. Prepared from the submitted Prototype Design 2 requirements and the System Demonstration and Viva rubric (pp. 1–2 and 7). The firmware is a NUCLEO-L476RG engineering baseline, NOT a tested custom PCB.',s['DBody']))
    story.append(Paragraph(f'Proposed live test duration: <b>{sum(float(x["minutes"]) for x in tests):.1f} minutes</b> (10–20 minutes recommended by course, absolute maximum 30 minutes). Long-duration battery/environmental qualification is reviewed as existing original evidence and cannot be completed live.',s['DBody']))
    story.append(Paragraph('Approval gates before submitting',s['DH1']))
    for msg in [
      'Obtain agreement that EVERY retained requirement is a realistic system-level requirement with a measurable external acceptance method. Keep unmet items openly marked as blockers. Do not silently remove previously agreed requirements.',
      'Confirm target physical board, exact standalone power source, physical controls, microphone(s), telecoil/loop, Bluetooth profile and acoustic coupling fixture. Rehearse all tests using the same firmware revision.',
      'Replace all unquantified terms (“safe output”, “fast recovery”, “humid”) with approved numerical thresholds and calibrated methods.',
      'Bring independent evidence for >=24 h battery life, environmental testing and standard applicability; no software unit test or simulated plot replaces a black-box hardware result.',
      'Test without ST-Link or UART/debugger. A phone may be used only where naturally part of the Bluetooth system. Perform damaging-condition tests last with appropriate protection.',
    ]:
      story.append(Paragraph('• '+ptxt(msg),s['DBody']))
    story.append(Paragraph('A. System-level requirements and test coverage',s['DH1']))
    story.append(Paragraph('The source group requirements repeat MEC-02 for two different mechanical statements; this draft uses MEC-02A/MEC-02B to disambiguate them. ADV-01/02 are proposed HD extensions and must be approved.',s['DMuted']))
    header=['ID','System requirement','Test case(s)','Current evidence / blocker']
    values=[header]
    for row in rows:
        values.append([row['Requirement Number'],row['Requirement Description'],row['Test Cases'],row['Implementation/evidence gate']])
    story.append(make_table(values,[19*mm,66*mm,27*mm,66*mm],s))
    story.append(PageBreak())
    story.append(Paragraph('B. External black-box test procedures',s['DH1']))
    story.append(Paragraph('The operator must observe sound, movement, user controls, current or external instruments. Do NOT inspect internal task lists, MCU variables, terminal printouts or source code to pass these tests. Enter measured values and PASS / FAIL / BLOCKED on a printed evidence sheet.',s['DBody']))
    for t in tests:
        cells=[
           ['Test case',f"{t['id']} — {t['name']} ({t['minutes']:g} min)"],
           ['Requirements addressed',', '.join(t['requirements'])],
           ['Prerequisites',t['prerequisites']],
           ['Procedure','\n'.join(f'{i}. {line}' for i,line in enumerate(t['procedure'],1))],
           ['Expected results','\n'.join(f'{i}. {line}' for i,line in enumerate(t['expected'],1))],
           ['Open gate / limitation',t['gate']],
           ['Observed result','__________________________  PASS / FAIL / BLOCKED'],
        ]
        table=Table([[Paragraph(ptxt(k),s['DCellBold']),Paragraph(ptxt(v),s['DCell'])] for k,v in cells],colWidths=[43*mm,135*mm],hAlign='LEFT')
        table.setStyle(TableStyle([('VALIGN',(0,0),(-1,-1),'TOP'),('BOX',(0,0),(-1,-1),.6,BORDER),('LINEBELOW',(0,0),(-1,-2),.3,BORDER),('BACKGROUND',(0,0),(0,-1),PALE),('LEFTPADDING',(0,0),(-1,-1),7),('RIGHTPADDING',(0,0),(-1,-1),7),('TOPPADDING',(0,0),(-1,-1),5),('BOTTOMPADDING',(0,0),(-1,-1),5)]))
        story.extend([KeepTogether([Paragraph(t['id']+' — '+ptxt(t['name']),s['DH2']),table]),Spacer(1,5*mm)])
    story.append(Paragraph('C. Evidence record — required to make the claims defensible',s['DH1']))
    story.append(Paragraph('For each PASS, record date/time, tester, firmware git SHA, physical board/PCB revision, power source, stimulus, instrument model/calibration, raw measured value, acceptance threshold, result and evidence file. Prior qualification records must identify the same configuration. If any test is unperformed, label it BLOCKED rather than inferred PASS.',s['DBody']))
    story.append(Paragraph('Submission reminder: Moodle requires the revised requirements and test plan together in a single PDF. This generated document is a draft and does not claim group approval or compliance.',s['DMuted']))
    doc.build(story,onFirstPage=paginate,onLaterPages=paginate)
    return path

def draw_box(c,x,y,w,h,title,body,theme='normal'):
    c.setFillColor(PALE if theme!='focus' else colors.HexColor('#D2EEF0'));c.setStrokeColor(BORDER)
    c.roundRect(x,y,w,h,8,stroke=1,fill=1)
    c.setFillColor(NAVY);c.setFont(BOLD,10);c.drawCentredString(x+w/2,y+h-17,title)
    c.setFillColor(GRAY);c.setFont(FONT,7.3)
    for j,line in enumerate(body): c.drawCentredString(x+w/2,y+h-32-12*j,line)

def arrow(c,x1,y1,x2,y2,label=None):
    c.setStrokeColor(TEAL);c.setLineWidth(1.6);c.line(x1,y1,x2,y2)
    import math
    a=math.atan2(y2-y1,x2-x1);L=7
    p=c.beginPath();p.moveTo(x2,y2);p.lineTo(x2-L*math.cos(a-.48),y2-L*math.sin(a-.48));p.lineTo(x2-L*math.cos(a+.48),y2-L*math.sin(a+.48));p.close()
    c.setFillColor(TEAL);c.drawPath(p,fill=1,stroke=0)
    if label:
        c.setFont(FONT,7);c.setFillColor(GRAY);c.drawCentredString((x1+x2)/2,(y1+y2)/2+6,label)

def make_a3(out):
    path=out/'PRINT_A3_SYSTEM_SOFTWARE_HARDWARE_INTERFACES.pdf'
    c=canvas.Canvas(str(path),pagesize=landscape(A3))
    w,h=landscape(A3)
    c.setFillColor(NAVY);c.rect(0,h-56,w,56,stroke=0,fill=1)
    c.setFont(BOLD,18);c.setFillColor(colors.white);c.drawString(23,h-31,'DIGITAL HEARING AID — END-TO-END SYSTEM & RTOS OWNERSHIP')
    c.setFont(FONT,8);c.drawRightString(w-18,h-31,'A3 landscape  |  NUCLEO-L476RG G5/G6 operating configuration')
    margin=30
    top=h-87
    draw_box(c,45,top-85,185,75,'Bluetooth audio / BT401',['UART1: module commands','SAI1A RX: PCM1808 + DMA'])
    draw_box(c,45,top-190,185,75,'Telecoil front-end',['Magnetic loop -> ADC PCM1808','SAI2A RX + DMA'])
    draw_box(c,45,top-295,185,75,'Physical USER button',['NUCLEO PC13 GPIO','ControlTask debounce/gestures'])
    draw_box(c,280,top-180,235,135,'STM32L476RG / FreeRTOS',['SAI RX DMA ISRs publish snapshots','Direct AudioTask notification from ISR','AudioTask priority 7, sole PCM owner','Source FSM + DSP + guardian','ControlTask priority 6 via GroupEvent queue','BtTask priority 5 UART supervision'],theme='focus')
    draw_box(c,570,top-125,214,76,'SAI2B output',['DMA -> MAX98357A amplifier','Bone-conduction transducer'])
    draw_box(c,570,top-250,214,76,'External black-box observable',['Sound / control response','Power / calibrated measurement'])
    draw_box(c,818,top-125,240,76,'Power source',['Intended battery or AC (verify)','No ST-Link / serial debug in demo'])
    draw_box(c,818,top-250,240,76,'Engineering evidence (offline)',['Source-level C + MATLAB analysis','Not a live black-box PASS criterion'])
    arrow(c,230,top-50,280,top-90,'DMA snapshot')
    arrow(c,230,top-150,280,top-118,'DMA snapshot')
    arrow(c,230,top-258,280,top-150,'event queue')
    arrow(c,515,top-95,570,top-83,'SAI2B DMA')
    arrow(c,677,top-125,677,top-174,'acoustic result')
    arrow(c,784,top-212,818,top-212,'record')
    c.setFillColor(NAVY);c.setFont(BOLD,10)
    c.drawString(45,128,'RTOS data path:')
    c.setFont(FONT,9);c.drawString(155,128,'DMA IRQ -> fixed capture rings -> task notification -> AudioTask -> speaker DMA')
    c.setFont(BOLD,10);c.drawString(45,107,'RTOS control path:')
    c.setFont(FONT,9);c.drawString(173,107,'Physical button -> ControlTask -> FreeRTOS GroupEvent queue -> AudioTask')
    c.setStrokeColor(BORDER);c.line(42,91,w-42,91)
    c.setFillColor(RED);c.setFont(BOLD,10);c.drawString(45,72,'HARDWARE LIMITATIONS / DO NOT CLAIM AS VERIFIED')
    c.setFillColor(GRAY);c.setFont(FONT,8)
    c.drawString(45,55,'Custom PCB PC13/PA5 not wired as Nucleo USER/LED; microphone DFSDM not yet integrated. MPR121 address pin, wear sensor, charging, SPL, endurance require hardware proof.')
    c.drawString(45,40,'M3 source idle is not true MCU low-power STOP. Operating build creates NO background load or continuous-spin tasks. Print TWO copies for demonstration; A3 or larger for viva.')
    c.save();return path

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--output',default=str(ROOT/'generated'));args=ap.parse_args()
    out=Path(args.output);out.mkdir(parents=True,exist_ok=True)
    for file in (make_assessment(out),make_a3(out)):print(file)
if __name__=='__main__':main()
