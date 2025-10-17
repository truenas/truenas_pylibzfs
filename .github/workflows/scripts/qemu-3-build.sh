#!/usr/bin/env bash

######################################################################
# Build and install truenas_pylibzfs in the VM
######################################################################

set -eu

echo "Building and installing truenas_pylibzfs..."

# Load VM info
source /tmp/vm-info.sh

# Wait for cloud-init to finish
echo "Waiting for cloud-init to complete..."
ssh debian@$VM_IP "cloud-init status --wait" || true

# Install rsync in VM first
echo "Installing rsync in VM..."
ssh debian@$VM_IP "sudo apt-get update && sudo apt-get install -y rsync"

# Copy source code to VM
echo "Copying source code to VM..."
ssh debian@$VM_IP "mkdir -p ~/truenas_pylibzfs"
rsync -az --exclude='.git' --exclude='debian/.debhelper' \
  --exclude='src/.libs' --exclude='*.o' --exclude='*.lo' \
  "$GITHUB_WORKSPACE/" debian@$VM_IP:~/truenas_pylibzfs/

# Install dependencies and build
echo "Installing dependencies in VM..."
ssh debian@$VM_IP 'bash -s' <<'REMOTE_SCRIPT'
set -eu

cd ~/truenas_pylibzfs

# Update package lists
sudo apt-get update

# Install build dependencies
sudo apt-get install -y \
  build-essential \
  devscripts \
  debhelper \
  dh-autoreconf \
  dh-python \
  autoconf \
  automake \
  libtool \
  pkg-config \
  uuid-dev \
  libssl-dev \
  libaio-dev \
  libblkid-dev \
  libelf-dev \
  libpam0g-dev \
  libtirpc-dev \
  libudev-dev \
  lsb-release \
  po-debconf \
  zlib1g-dev \
  python3-dev \
  python3-all-dev \
  python3-cffi \
  python3-setuptools \
  python3-sphinx \
  git \
  linux-headers-amd64 \
  dkms

# Build and install OpenZFS
echo "Building OpenZFS..."
cd /tmp
git clone --depth 1 --branch truenas/zfs-2.4-release https://github.com/truenas/zfs.git
cd zfs
# Copy debian packaging files to root
cp -r contrib/debian debian
# Run autogen
sh autogen.sh
# Configure
./configure
# Copy changelog and prepare rules
cp contrib/debian/changelog debian/changelog
sed 's/@CFGOPTS@/--enable-debuginfo/g' debian/rules.in > debian/rules
chmod +x debian/rules
# Build OpenZFS packages
dpkg-buildpackage -us -uc -b
# Install required libraries in correct dependency order
sudo dpkg -i ../openzfs-libnvpair3_*.deb
sudo dpkg -i ../openzfs-libuutil3_*.deb
sudo dpkg -i ../openzfs-libzpool6_*.deb
sudo dpkg -i ../openzfs-libzfs6_*.deb
sudo dpkg -i ../openzfs-libzfsbootenv1_*.deb
sudo dpkg -i ../openzfs-libzfs-dev_*.deb
# Install DKMS package to build and load kernel modules
sudo dpkg -i ../openzfs-zfs-dkms_*.deb || true
# Wait for DKMS to build modules
sleep 5
# Load ZFS kernel module
sudo modprobe zfs || true

# Build truenas_pylibzfs
echo "Building truenas_pylibzfs..."
cd ~/truenas_pylibzfs
dpkg-buildpackage -us -uc -b

# Install truenas_pylibzfs package
echo "Installing truenas_pylibzfs..."
sudo dpkg -i ../python3-truenas-pylibzfs_*.deb

# Verify installation
echo "Verifying installation..."
python3 -c "import truenas_pylibzfs" || (echo "ERROR: truenas_pylibzfs not importable"; exit 1)

echo "Build and installation complete!"
REMOTE_SCRIPT

echo "truenas_pylibzfs installed successfully in VM"
