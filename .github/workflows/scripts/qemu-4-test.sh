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
echo "Configuring core dump capture"
echo "=========================================="

# Allow core files of any size from the test process.
ulimit -c unlimited
ulimit -c

# Direct cores to a known path inside the VM, bypassing any
# systemd-coredump pipe handler so we get raw files we can inspect
# with gdb.  %e=executable, %p=pid, %t=epoch.
sudo mkdir -p /tmp/cores
sudo chmod 1777 /tmp/cores
sudo systemctl mask systemd-coredump.socket 2>/dev/null || true
echo '/tmp/cores/core.%e.%p.%t' | sudo tee /proc/sys/kernel/core_pattern
echo "core_pattern: $(cat /proc/sys/kernel/core_pattern)"

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
    # Faulthandler prints a Python+C traceback on fatal signals before the
    # process dies; useful even when the kernel does not write a core file.
    # Disable -e around pytest so a segfault (non-zero exit) does not skip
    # the core-dump backtrace section that follows.
    set +e
    PYTHONFAULTHANDLER=1 python3 -m pytest tests/ -v --tb=short --timeout=120
    TEST_EXIT_CODE=$?
    set -e
else
    echo ""
    echo "No tests directory found, skipping pytest"
    TEST_EXIT_CODE=0
fi

echo "=========================================="
echo "Core dump backtraces"
echo "=========================================="
# Mirror gdb output to a file as well as stdout so it ends up in both
# the GitHub Actions log and the uploaded qemu-logs artifact.
BT_LOG=/home/debian/coredump-backtraces.log
: > "$BT_LOG"
shopt -s nullglob
CORES=(/tmp/cores/core.*)
if [ ${#CORES[@]} -eq 0 ]; then
    echo "No core files were produced." | tee -a "$BT_LOG"
else
    for core in "${CORES[@]}"; do
        {
            echo ""
            echo "------ $core ------"
            ls -lh "$core"
            # Recover the crashed executable from the core_pattern filename
            # (core.<exe>.<pid>.<epoch>); fall back to python3 if it's not
            # on PATH (e.g. the crash was in a subprocess we can't locate).
            exe_name=$(basename "$core" | awk -F. '{print $2}')
            exe_path=$(command -v "$exe_name" || echo /usr/bin/python3)
            echo "Executable: $exe_path"
            # thread apply all bt full prints every thread's full backtrace
            # including locals, which is what we need to diagnose the
            # C-extension segfault.
            gdb -batch -nx \
                -ex "set pagination off" \
                -ex "set print pretty on" \
                -ex "thread apply all bt full" \
                "$exe_path" "$core" 2>&1 || true
        } | tee -a "$BT_LOG"
    done
fi
shopt -u nullglob
sudo chown debian:debian "$BT_LOG" 2>/dev/null || true

echo "=========================================="
echo "Test run complete (exit code: $TEST_EXIT_CODE)"
echo "=========================================="

echo "=========================================="
echo "Running stub checks"
echo "=========================================="

cd /home/debian/truenas_pylibzfs

# Check stubs are internally self-consistent (catches phantom names in __all__, bad imports, etc.)
echo "Running mypy on stubs/..."
python3 -m mypy stubs/
MYPY_EXIT=$?

# Check that typed callers use the stubs correctly (catches signature mismatches
# that stubtest cannot detect, e.g. wrong container type for holds arguments).
echo "Running mypy on tests/type_checks/..."
python3 -m mypy tests/type_checks/
MYPY_TYPE_CHECKS_EXIT=$?

# Check stubs match the installed runtime module.
#
# truenas_pylibzfs is a C extension (.so), not a Python package, so
# `import truenas_pylibzfs.lzc` (dotted path) fails even though
# `from truenas_pylibzfs import lzc` works fine.  stubtest uses
# importlib.import_module() internally, so pre-registering the
# submodules in sys.modules lets stubtest find and check them.
echo "Running stubtest for truenas_pylibzfs..."
python3 -c "
import truenas_pylibzfs, sys
for name in ('lzc', 'libzfs_types', 'property_sets', 'kstat'):
    sys.modules['truenas_pylibzfs.' + name] = getattr(truenas_pylibzfs, name)
from mypy.stubtest import main
sys.argv = ['stubtest', 'truenas_pylibzfs']
sys.exit(main())
"
STUBTEST_EXIT=$?

if [ $MYPY_EXIT -ne 0 ] || [ $MYPY_TYPE_CHECKS_EXIT -ne 0 ] || [ $STUBTEST_EXIT -ne 0 ]; then
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
