#!/usr/bin/env bash

######################################################################
# Download and start Debian Trixie VM
######################################################################

set -eu

echo "Starting Debian Trixie VM..."

# Configuration
OS="debian-trixie"
OSNAME="Debian Trixie"
URL="https://cloud.debian.org/images/cloud/trixie/latest/debian-13-generic-amd64.qcow2"
VM_NAME="truenas-pylibzfs-test"
VM_IP="192.168.122.10"
VM_MAC="52:54:00:83:79:10"

# Create working directory
WORK_DIR="/tmp/qemu-work"
mkdir -p "$WORK_DIR"
cd "$WORK_DIR"

# Download Debian Trixie cloud image
echo "Downloading Debian Trixie cloud image..."
if [ ! -f "debian-trixie.qcow2" ]; then
  wget -q --show-progress "$URL" -O debian-trixie.qcow2
fi

# Create a larger disk from the base image
echo "Creating VM disk..."
qemu-img create -f qcow2 -F qcow2 -b "$WORK_DIR/debian-trixie.qcow2" "$WORK_DIR/vm-disk.qcow2" 40G

# Get SSH public key
PUBKEY=$(cat ~/.ssh/id_ed25519.pub)

# Create cloud-init user-data (in /tmp like ZFS does)
cat <<EOF > /tmp/user-data
#cloud-config

hostname: $OS

users:
- name: debian
  sudo: ALL=(ALL) NOPASSWD:ALL
  shell: /bin/bash
  ssh_authorized_keys:
    - $PUBKEY

packages:
  - python3

runcmd:
  - echo "VM initialization complete"

growpart:
  mode: auto
  devices: ['/']
  ignore_growroot_disabled: false
EOF

# Configure libvirt network
sudo virsh net-update default add ip-dhcp-host \
  "<host mac='$VM_MAC' ip='$VM_IP'/>" --live --config || true

# Start the VM (using --cloud-init like ZFS does)
# Note: Debian Trixie requires UEFI boot (like ZFS workflow does for debian13)
echo "Starting VM..."
sudo virt-install \
  --name "$VM_NAME" \
  --os-variant debian12 \
  --cpu host-passthrough \
  --virt-type=kvm \
  --vcpus=4 \
  --memory 8192 \
  --graphics none \
  --network bridge=virbr0,model=virtio,mac="$VM_MAC" \
  --cloud-init user-data=/tmp/user-data \
  --disk path="$WORK_DIR/vm-disk.qcow2",format=qcow2,bus=virtio \
  --boot uefi=on,firmware.feature0.name=secure-boot,firmware.feature0.enabled=no \
  --import \
  --noautoconsole >/dev/null

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
  echo "ERROR: VM is not accessible"
  exit 1
fi

# Give the VM a moment to fully initialize
echo "Waiting for VM to fully initialize..."
sleep 10

# Add VM hostname to /etc/hosts
echo "$VM_IP vm-test" | sudo tee -a /etc/hosts

# Save VM info for later scripts
cat <<EOF > /tmp/vm-info.sh
export VM_IP="$VM_IP"
export VM_NAME="$VM_NAME"
export WORK_DIR="$WORK_DIR"
EOF

echo "VM started successfully at $VM_IP"
