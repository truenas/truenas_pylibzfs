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
# Run autogen
./autogen.sh
# Configure
./configure --prefix=/usr --enable-pyzfs --enable-debuginfo
# Build native debian packages
make -j$(nproc) native-deb-kmod native-deb-utils
# Install packages (excluding dkms and dracut)
sudo apt-get -y install $(find ../ | grep -E '\.deb$' | grep -Ev 'dkms|dracut')

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
