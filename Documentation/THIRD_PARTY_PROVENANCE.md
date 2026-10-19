# Third-party components and distribution provenance

This inventory is based on the current repository layout and **must be confirmed by the project's human rights-holders** before publishing the repository. It is not a legal opinion, a blanket relicensing or a complete software bill of materials.

| Content | Source / observed notice | Public-distribution action |
| --- | --- | --- |
| `Drivers/STM32L4xx_HAL_Driver/` | STMicroelectronics; `Drivers/STM32L4xx_HAL_Driver/LICENSE.txt` refers to the original ST package licence, with BSD-3-Clause fallback if received outside the package without package terms | Preserve notice, verify exact distribution's `Package_license` and applicable terms |
| `Drivers/CMSIS/` | Arm CMSIS; `Drivers/CMSIS/LICENSE.txt` contains Apache-2.0 text | Keep full notice and verify per-file/component exceptions |
| `Drivers/CMSIS/Device/ST/STM32L4xx/` | ST device files; `LICENSE.txt` references the ST package licence with Apache-2.0 fallback, and `License.md` contains Apache-2.0 text | Preserve both; verify applicable package and source attribution |
| `Middlewares/Third_Party/FreeRTOS/` | **Not committed**; retrieved from locally installed STM32Cube FW_L4 by `PREPARE_PROJECT.ps1` | Do not bundle a guessed copy or strip upstream licences; separately inventory the actual installed version used for binaries |
| `Hardware/Original_KiCad/` | Group PCB design plus unresolved references to external footprint/symbol libraries | Validate permissions for the group design and each externally sourced footprint/symbol; run real ERC/DRC |
| `References/GroupMemberPrototypes/` | Copies of student/group-member experiments, not linked as main firmware | Contact each author before redistributing or relicensing |
| `Documentation/Reference/*.pdf` | Provided assessment/rubric and group-design documents | Clear with rights-holder/course/institution; remove from prospective public tree if not cleared |
| `Dashboard/requirements.txt` | pyserial, numpy, matplotlib are separately supplied Python packages | Obtain applicable licences, check compatible versions and supply-chain advisories at release time |
| `Assessment/generate_pack.py` | Uses separately installed ReportLab (4.x currently) for generated PDFs | Review ReportLab's current distribution terms and pin a tested release before distributing locked artefacts |

**Team-authored firmware, documentation, GUI, schematic and tests do not have a project-wide licence in this repository.** Do not add MIT/GPL/Apache or equivalent headers unilaterally. A GitHub public toggle alone does **not** grant downstream reuse rights.

Store actual permissions/consents outside the public repository and include only a reviewer-approved summary of what content is cleared. If rights to old artefacts or historical commits cannot be established, create a new, explicitly cleared public distribution repository and check its full new history before opening it.
