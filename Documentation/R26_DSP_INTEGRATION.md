# R26 source integration status — 3 November 2026

## Scope

The R26 portable DSP and online-learning components are imported alongside the older root CubeIDE project. The telecoil cleanup module in the root is replaced with the newer adaptive R26 implementation; its matching recovery copy is replaced too. New ambient and scene-learning modules are staged and compile, but are NOT connected to the older root project's microphone processing path.

Component | Repo status
--- | ---
g1_ambient_dsp.[ch] | Added: bounded 20x AGC and ambient-feature extraction; not wired into older root application
g1_scene_ml.[ch] | Added: three online unsupervised acoustic-feature clusters; not a labelled speech/traffic classifier
tc_cleanup.[ch] | Replaced older filter with R26 two-pole low-pass, soft noise gate, bounded level recovery
adaptive_preferences.[ch] | Retained existing code, already functionally identical to supplied R26
Tests/test_r26_dsp.c | Added strict host sanitizer regression with deterministic microphone/tone/hiss inputs
Recovery/Core | Matching copies restored after CubeMX generation

## Why the remaining R26 files were not overwritten

The repository root is a legacy Nucleo-oriented project. The user-tested R24 standalone custom-PCB firmware and the newer R26 custom-PCB candidate have different clock, pin, DFSDM, SAI, DMA, UI and application ownership. Copying only their demo_app, main, g1_product_board or .ioc files over the older root would be an incoherent mixed firmware project. This commit does NOT claim that the root is a complete R26 flashable executable. The full matching R26 project must be imported as a unit and native CubeIDE-built before it can replace the root.

## Verified correction

In the original R26 ambient DSP, left-shifting a signed negative PCM value caused an undefined-behaviour sanitizer fault. The GitHub copy corrects (raw << 8) to (raw * 256) without changing the scaling. Strict GCC compilation and AddressSanitizer/UndefinedBehaviorSanitizer tests pass on deterministic inputs.

## Hardware status

The working physical-board baseline reported by the operator is R24. R26's increased sensitivity, TC noise suppression and acoustic clustering have not yet been measured on the physical PCB. Telecoil LOW16 decoding belongs to the complete custom-PCB runtime and is NOT provided by this selective root-DSP change. Audio output levels remain uncalibrated and this is not a medical device approval.
