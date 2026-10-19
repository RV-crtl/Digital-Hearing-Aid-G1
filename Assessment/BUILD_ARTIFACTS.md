# How to get exact draft PDF artifacts from GitHub

The workflow `.github/workflows/assessment-pack.yml` runs a coverage check, renders the source CSV/JSON into two printable PDFs, uploads the artifacts, and retains them for the configured period.

After your hardware team revises the requirements and confirms the test plan:

1. Modify `Assessment/requirements.csv` and `Assessment/test_cases.json` in a reviewed PR. Confirm test coverage and 10–20-minute intended duration, no internal debugger/probing test steps.
2. Open **Actions → Build HD demonstration evidence pack → Run workflow** and download `draft-assessment-pdfs`.
3. Review all PDF pages. Confirm numerical threshold and actual hardware assumptions.
4. Rename/submit the combined `SYSTEM_REQUIREMENTS_AND_BLACK_BOX_TEST_PLAN_DRAFT.pdf` only after group approval. Print two A3 diagrams (`PRINT_A3_SYSTEM_SOFTWARE_HARDWARE_INTERFACES.pdf`) for the day.
5. Fill the evidence register from physical tests; don't upload fake PASS statements.

The artifacts are reproducible from the tracked source and do not automatically indicate compliance.
