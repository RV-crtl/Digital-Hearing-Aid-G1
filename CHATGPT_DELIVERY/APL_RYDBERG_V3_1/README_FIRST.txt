APL RYDBERG FINAL CONVERGENCE V3.1
=================================

PURPOSE
-------
This patch repairs the reporting/resume weakness in the old V3 campaign.
It does NOT rerun the aperture study and does NOT broaden the candidate search.

It keeps the paper-critical configuration:
  Candidate: C0002
  Operating point: 1.675 GHz
  Readout basis: physically allowed pi / sigma+ / sigma-
  Final gate: independent mesh agreement and full-Stokes reconstruction quality

FILES TO DOWNLOAD
-----------------
Root of your existing extracted V3 folder:
  RUN_THIS_FINAL_V3_1.cmd

Inside:
  02_FULL_STOKES_FINAL\v31_resume_and_verdict.py
  02_FULL_STOKES_FINAL\v31_record_failure.py
  02_FULL_STOKES_FINAL\pack_v31_return.py

You do NOT need to delete your old V3 results. Keeping them is useful because
V3.1 checks the existing EXPORT_MANIFEST.json files and skips complete jobs.

EXACT INSTALLATION
------------------
Start with your existing extracted folder, for example:
  C:\APL_V3\APL_RYDBERG_FINAL_CONVERGENCE_V3\

Place:
  RUN_THIS_FINAL_V3_1.cmd
at:
  C:\APL_V3\APL_RYDBERG_FINAL_CONVERGENCE_V3\RUN_THIS_FINAL_V3_1.cmd

Place these three Python files:
  v31_resume_and_verdict.py
  v31_record_failure.py
  pack_v31_return.py
inside:
  C:\APL_V3\APL_RYDBERG_FINAL_CONVERGENCE_V3\02_FULL_STOKES_FINAL\

HOW TO RUN
----------
1. Close every Ansys Electronics Desktop / HFSS window.
2. Keep the old V3 results folders in place.
3. Double-click:
     RUN_THIS_FINAL_V3_1.cmd
4. Do not manually open AEDT while it is running.

RESUME BEHAVIOUR
----------------
For convA, convB and convC, V3.1 checks all eight field-export jobs:
  four analyzer states x X/Y incident basis

If a level is already complete, it prints:
  CHECKPOINT: convX already complete; no HFSS rerun.

If a level is incomplete, only the normal V3 HFSS candidate runner for that
level is invoked. The existing production solver then uses its own checkpoint
logic to skip completed state/polarization jobs.

SCIENTIFIC GATES -- UNCHANGED
-----------------------------
The patch does NOT loosen the paper thresholds:
  rank(A) = 4
  independent normalized instrument-matrix error <= 5 %
  physically allowed atomic readout condition number <= 6
  mean Poincare error at 1 % measurement noise <= 3 deg
  95th-percentile Poincare error at 1 % noise <= 10 deg

A/B is analysed first.
If A/B passes every gate, the campaign stops and reports PASS.
If A/B fails, convC is completed and B/C becomes the final scientific verdict.
If B/C still fails, status is SCIENTIFIC_FAIL and the simulation campaign stops.

RETURN FILES -- IMPORTANT
-------------------------
V3.1 ALWAYS attempts to put the important information OUTSIDE the ZIP as well.

Upload these THREE files back to ChatGPT:
  1. newest RETURN_TO_CHATGPT_APL_V3_1_<STATUS>_<timestamp>.zip
  2. FINAL_PAPER_VERDICT.json
  3. RUN_STATUS.json

The JSON sidecars are deliberate. Even if ZIP extraction is unavailable in a
future chat session, the scientific result and exact failed stage remain directly
readable.

The return ZIP also puts these at ZIP root where available:
  RETURN_MANIFEST_V3_1.json
  FINAL_PAPER_VERDICT.json
  RUN_STATUS.json
  FINAL_PAPER_VERDICT.txt
  CONVERGENCE_AB.json
  CONVERGENCE_BC.json
  CONVERGENCE_LATEST.json
  FEBI_HYBRID_SELF_TEST.json
  V31_FATAL_ERROR.txt
  V31_SETUP.log

STATUS MEANING
--------------
PASS
  All predeclared scientific gates passed. Freeze the result for the paper.

SCIENTIFIC_FAIL
  The simulations completed, but the final independent comparison still fails
  at least one predeclared gate. Do not keep changing thresholds to force a pass.

OPERATIONAL_FAIL
  Environment/HFSS/automation failed before a valid scientific verdict.
  This is not a physics failure. FINAL_PAPER_VERDICT.json identifies the exact
  stage and carries available diagnostics.

NO APERTURE RERUN
-----------------
The aperture study remains frozen. Its outer-domain convergence passed, but the
Q-based constant-Gamma aperture law did not pass its numerical/physical gates.
It is supporting/design-limit evidence, not the main APL claim.
