# G1 maintainer and public project checklist

The repository is currently public. GitHub Settings changes are still manual.

1. About panel (repository home, gear icon): description = Standalone STM32 audio | Bluetooth, telecoil, DFSDM microphone design, FreeRTOS, custom PCB, adaptive listening research. Set Website to the deployed Pages URL after successful workflow. Suggested topics: stm32, embedded-systems, freertos, digital-signal-processing, kicad, github-pages, adaptive-audio, lifepo4.
2. Settings > Rules: require engineering CI and website checks for relevant changes, review before merge, disallow force pushes to main. Ask for physical regression evidence on firmware/peripheral changes.
3. Security: enable dependency alerts, private vulnerability reporting and limited Actions permissions.
4. Pages: source = GitHub Actions; run the publish workflow manually. Public Pages must not contain student records, restricted PCB assets or audio recordings.
5. Releases: this directory supplies release-note scaffolding only. Never publish an untested binary as a validated hearing aid. Record board revision, 2S pack condition, actual ARM ELF hash, electronics/acoustic tests and sign-off. 
6. Licence: public visibility does not grant an open-source licence. Obtain contributor/institution rights to firmware and CAD before redistribution.
7. Dependabot will suggest GitHub Actions and Python dependency upgrades; never auto-merge without CI and impact review.

The website architecture and AI feedback demonstration are visual training tools, not remote hardware control or measured acoustic results.
