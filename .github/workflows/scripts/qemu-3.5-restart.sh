#!/usr/bin/env bash

######################################################################
# Wait for VM poweroff and restart it (kernel modules need reboot)
######################################################################

set -eu

echo "Waiting for VM shutdown and restarting..."

# Load VM info
source /tmp/vm-info.sh

# Wait for VM to shut down
echo "Waiting for VM to shut down..."
for i in {1..60}; do
  if sudo virsh list --all | grep "$VM_NAME" | grep -q "shut off"; then
    echo "VM has shut down"
    break
  fi
  echo "Waiting for shutdown... ($i/60)"
  sleep 2
done

# Verify it's actually shut off
if ! sudo virsh list --all | grep "$VM_NAME" | grep -q "shut off"; then
  echo "VM did not shut down gracefully, forcing shutdown..."
  sudo virsh destroy "$VM_NAME" || true
  sleep 3
fi

# Start the VM
echo "Starting VM..."
sudo virsh start "$VM_NAME"

# Give it time to start booting
sleep 5

# Wait for VM to be accessible via SSH
echo "Waiting for VM to be ready..."
for i in {1..60}; do
  if ssh -o ConnectTimeout=2 debian@$VM_IP "echo 'VM ready'" 2>/dev/null; then
    echo "VM is accessible via SSH"
    break
  fi
  echo "Waiting for VM... ($i/60)"
  sleep 5
done

# Verify VM is accessible
if ! ssh debian@$VM_IP "uname -a"; then
  echo "ERROR: VM is not accessible after restart"
  exit 1
fi

echo "VM restarted successfully at $VM_IP"
