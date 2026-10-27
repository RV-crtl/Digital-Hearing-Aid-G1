#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
# Single source of truth for portable compilation/testing: Python runner.
# The former duplicated shell build commands could silently go stale.
python3 Validation/run_host_validation.py "$@"
