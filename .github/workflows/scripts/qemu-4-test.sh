#!/usr/bin/env bash

######################################################################
# Run tests in the VM
######################################################################

set -eu

echo "Running tests..."

# Load VM info
source /tmp/vm-info.sh

# Run tests in VM
ssh debian@$VM_IP 'sudo bash -s' <<'REMOTE_SCRIPT'
set -eu

echo "=========================================="
echo "Loading ZFS kernel modules"
echo "=========================================="

# Load ZFS kernel module (after reboot, modules should load cleanly)
echo "Loading ZFS kernel module..."
sudo modprobe zfs || {
  echo "ERROR: Failed to load ZFS kernel module"
  sudo dmesg | tail -20
  exit 1
}

# Verify module is loaded
if ! lsmod | grep zfs; then
  echo "ERROR: ZFS module not loaded"
  exit 1
fi

echo "ZFS kernel module loaded successfully"
lsmod | grep zfs

cd /home/debian/truenas_pylibzfs

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
    python3 -m pytest tests/ -v --tb=short --timeout=120
    TEST_EXIT_CODE=$?
else
    echo ""
    echo "No tests directory found, skipping pytest"
    TEST_EXIT_CODE=0
fi

echo "=========================================="
echo "Test run complete (exit code: $TEST_EXIT_CODE)"
echo "=========================================="

echo "=========================================="
echo "Running stub checks"
echo "=========================================="

cd /home/debian/truenas_pylibzfs

# Check stubs are internally self-consistent (catches phantom names in __all__, bad imports, etc.)
echo "Running mypy on stubs..."
python3 -m mypy stubs/
MYPY_EXIT=$?

# Check stubs match the installed runtime module.
#
# truenas_pylibzfs is a C extension (.so), not a Python package, so
# `import truenas_pylibzfs.lzc` (dotted path) fails even though
# `from truenas_pylibzfs import lzc` works fine.  stubtest uses
# importlib.import_module() internally, so pre-registering the
# submodules in sys.modules lets stubtest find and check them.
echo "Running stubtest..."
python3 -c "
import truenas_pylibzfs, sys
for name in ('lzc', 'libzfs_types', 'property_sets'):
    sys.modules['truenas_pylibzfs.' + name] = getattr(truenas_pylibzfs, name)
from mypy.stubtest import main
sys.argv = ['stubtest', 'truenas_pylibzfs']
main()
"
STUBTEST_EXIT=$?

if [ $MYPY_EXIT -ne 0 ] || [ $STUBTEST_EXIT -ne 0 ]; then
    echo "ERROR: Stub checks failed"
    exit 1
fi
echo "Stub checks passed"

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
