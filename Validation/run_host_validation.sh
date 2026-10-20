#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p Validation/results
CFLAGS=(-std=c11 -O2 -Wall -Wextra -Werror -I Core/Inc)
CC=${CC:-gcc}
"$CC" "${CFLAGS[@]}" Tests/test_group_features.c Core/Src/group_features.c -o # Compile-only all portable modules even when they are not directly linked by a host test.
mkdir -p Validation/results/portable_compile
PORTABLE_MODULES=(audio_processing pcm_transport audio_dynamics telecoil_filter telecoil_detector signal_quality connectivity_service connectivity_actions bt_profile control_protocol tc_cleanup group_features group_guardian group_ui group_encoder connectivity_controller)
for module in "${PORTABLE_MODULES[@]}"; do
  "$CC" "${CFLAGS[@]}" -c "Core/Src/$module.c" -o "Validation/results/portable_compile/$module.o"
done
Validation/results/test_features
"$CC" "${CFLAGS[@]}" Tests/test_encoder.c Core/Src/group_encoder.c -o Validation/results/test_encoder
"$CC" "${CFLAGS[@]}" Tests/test_group_guardian.c Core/Src/group_guardian.c -o Validation/results/test_guardian
"$CC" "${CFLAGS[@]}" Tests/test_group_ui.c Core/Src/group_ui.c -o Validation/results/test_group_ui
"$CC" "${CFLAGS[@]}" Tests/test_control_protocol.c Core/Src/control_protocol.c -o Validation/results/test_control_protocol
"$CC" "${CFLAGS[@]}" Tests/test_pcm_transport.c Core/Src/pcm_transport.c Core/Src/audio_processing.c -o Validation/results/test_pcm_transport
"$CC" "${CFLAGS[@]}" Validation/c/audio_vectors.c Core/Src/audio_dynamics.c -o Validation/results/audio_vectors
"$CC" "${CFLAGS[@]}" Validation/c/benchmark_profiles.c Core/Src/group_features.c -o Validation/results/benchmark_profiles
Validation/results/test_features
Validation/results/test_encoder
Validation/results/test_guardian
Validation/results/test_group_ui
Validation/results/test_control_protocol
Validation/results/test_pcm_transport
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
python3 Tests/test_publication_audit.py
python3 Tests/test_dashboard_static.py
python3 Validation/python/test_tree_integrity.py
python3 Validation/python/check_tree_integrity.py
python3 Tools/test_verify_arm_evidence.py
python3 Tools/publication_audit.py --output Validation/results/publication_audit.json
python3 -m compileall -q Dashboard
printf '\nPASS: G5 host DSP vectors, FSM, encoder, guardian, evidence trace and KiCad source audit.\n'
