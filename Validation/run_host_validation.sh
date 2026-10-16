#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p Validation/results
CFLAGS=(-std=c11 -O2 -Wall -Wextra -Werror -I Core/Inc)
CC=${CC:-gcc}
"$CC" "${CFLAGS[@]}" Tests/test_group_features.c Core/Src/group_features.c -o Validation/results/test_features
"$CC" "${CFLAGS[@]}" Tests/test_encoder.c Core/Src/group_encoder.c -o Validation/results/test_encoder
"$CC" "${CFLAGS[@]}" Tests/test_group_guardian.c Core/Src/group_guardian.c -o Validation/results/test_guardian
Validation/results/test_group_ui
"$CC" "${CFLAGS[@]}" Tests/test_group_ui.c Core/Src/group_ui.c -o Validation/results/test_group_ui
"$CC" "${CFLAGS[@]}" Validation/c/audio_vectors.c Core/Src/audio_dynamics.c -o Validation/results/audio_vectors
"$CC" "${CFLAGS[@]}" Validation/c/benchmark_profiles.c Core/Src/group_features.c -o Validation/results/benchmark_profiles
Validation/results/test_features
Validation/results/test_encoder
Validation/results/test_guardian
Validation/results/audio_vectors Validation/results/audio_vectors.csv
Validation/results/benchmark_profiles > Validation/results/profile_host_benchmark.csv
python3 Validation/python/check_vectors.py Validation/results/audio_vectors.csv
python3 CHECK_PROJECT.py
python3 CHECK_GROUP.py
"$CC" "${CFLAGS[@]}" Tests/test_connectivity_scenarios.c Core/Src/connectivity_controller.c -o Validation/results/test_connectivity
Validation/results/test_connectivity
python3 Validation/python/audit_kicad.py
python3 Validation/python/check_evidence.py
python3 Validation/python/check_repository.py
python3 Validation/python/verify_rtos_contract.py
printf '\nPASS: G5 host DSP vectors, FSM, encoder, guardian, evidence trace and KiCad source audit.\n'
