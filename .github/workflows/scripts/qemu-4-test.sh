#!/usr/bin/env bash

######################################################################
# Run tests in the VM
######################################################################

set -eu

echo "Running tests..."

# Load VM info
source /tmp/vm-info.sh

# Run tests in VM
ssh debian@$VM_IP 'bash -s' <<'REMOTE_SCRIPT'
set -eu

cd ~/truenas_pylibzfs

echo "=========================================="
echo "Running verification tests"
echo "=========================================="

# Verify Python module is importable
echo "Verifying truenas_pylibzfs module..."
python3 -c "import truenas_pylibzfs; print('Module imported successfully')"

# Check if tests directory exists and run pytest if it does
if [ -d "tests" ]; then
    echo ""
    echo "Running pytest tests..."
    python3 -m pytest tests/ -v
    TEST_EXIT_CODE=$?
else
    echo ""
    echo "No tests directory found, skipping pytest"
    TEST_EXIT_CODE=0
fi

echo "=========================================="
echo "Test run complete (exit code: $TEST_EXIT_CODE)"
echo "=========================================="

exit $TEST_EXIT_CODE
REMOTE_SCRIPT

# Capture test results
TEST_EXIT_CODE=$?

if [ $TEST_EXIT_CODE -eq 0 ]; then
  echo "All tests passed!"
else
  echo "Tests failed with exit code: $TEST_EXIT_CODE"
  exit $TEST_EXIT_CODE
fi
