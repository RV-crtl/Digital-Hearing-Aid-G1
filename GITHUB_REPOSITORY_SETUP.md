# New GitHub repository setup — Group G1

This project MUST live in a NEW, PRIVATE GitHub repository. **Do not push to or branch from** `RV-crtl/260827-DHA_BT_Telecoil_PD5`.

## Repository creation

1. Open https://github.com/new while signed into GitHub as `RV-crtl`.
2. Repository owner: `RV-crtl`. Name: `Digital-Hearing-Aid-G1`. Visibility: **Private**.
3. Leave “Add a README”, “Add .gitignore”, and “Choose a license” unchecked so the new repo starts empty.
4. Click **Create repository**.
5. Return the new repository URL to ChatGPT. The GitHub connector can push content once the repo exists; it cannot create the repository.

This archive contains the root of the STM32CubeIDE project, not an additional nested archive. Import it using **File > Import > General > Existing Projects into Workspace** after checking out/extracting it.

## Project workflow

* Use the project's `00_READ_ME_FIRST.md` and `PREPARE_PROJECT.bat` for the CubeIDE build process.
* If CubeMX code generation is necessary, complete it BEFORE running `PREPARE_PROJECT.bat`.
* Verify preparation checks, then select Debug, Clean, Build and Flash.
* This remains a **Nucleo-L476RG G1 integration baseline**, not proven custom-PCB firmware. See `Documentation` for limitations.
* Never commit generated `Debug/`, `Release/`, `RESULTS_R26/`, runtime logs, or installed FreeRTOS middleware.

## Version control note

No GitHub repository is created by this file. Publishing to GitHub requires either its connected upload actions or a local `git push` after repository creation.
