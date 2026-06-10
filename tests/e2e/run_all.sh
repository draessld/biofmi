#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

EXIT_CODE=0

for test_script in "$SCRIPT_DIR"/test_*.sh; do
    bash "$test_script" || EXIT_CODE=1
    echo ""
done

exit $EXIT_CODE
