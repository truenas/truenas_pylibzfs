#!/usr/bin/env bash

######################################################################
# Collect logs from VM
######################################################################

set -eu

echo "Collecting logs..."

# Load VM info
source /tmp/vm-info.sh

# Create logs directory
LOG_DIR="/tmp/test-logs"
mkdir -p "$LOG_DIR"

# Copy test output
cp /tmp/test-output.txt "$LOG_DIR/" 2>/dev/null || true
cp /tmp/test-exitcode.txt "$LOG_DIR/" 2>/dev/null || true

# Collect system logs from VM
ssh debian@$VM_IP "sudo journalctl -n 1000" > "$LOG_DIR/journalctl.log" 2>/dev/null || true
ssh debian@$VM_IP "dmesg" > "$LOG_DIR/dmesg.log" 2>/dev/null || true
ssh debian@$VM_IP "cat /var/log/auth.log" > "$LOG_DIR/auth.log" 2>/dev/null || true

# Create tarball
cd /tmp
tar czf qemu-logs.tar.gz test-logs/

echo "Logs collected at /tmp/qemu-logs.tar.gz"
