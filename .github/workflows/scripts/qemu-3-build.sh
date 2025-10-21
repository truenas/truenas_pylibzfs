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

# Check if we have cached ZFS packages
if [ "$ZFS_CACHE_HIT" = "true" ] && [ -d "/tmp/zfs-debs" ]; then
  echo "Found cached OpenZFS packages, copying to VM..."
  ssh debian@$VM_IP "mkdir -p /tmp/zfs-debs"
  rsync -az /tmp/zfs-debs/ debian@$VM_IP:/tmp/zfs-debs/
  CACHED_ZFS="true"
else
  echo "No cached OpenZFS packages found, will build from source"
  CACHED_ZFS="false"
fi

# Copy source code to VM
echo "Copying source code to VM..."
ssh debian@$VM_IP "mkdir -p ~/truenas_pylibzfs"
rsync -az --exclude='.git' --exclude='debian/.debhelper' \
  --exclude='src/.libs' --exclude='*.o' --exclude='*.lo' \
  "$GITHUB_WORKSPACE/" debian@$VM_IP:~/truenas_pylibzfs/

# Install dependencies and build
echo "Installing dependencies in VM..."
ssh debian@$VM_IP bash -s "$CACHED_ZFS" <<'REMOTE_SCRIPT'
CACHED_ZFS="$1"
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
  python3-pytest \
  git \
  linux-headers-amd64 \
  dkms

# Check if we have cached ZFS packages
if [ -d "/tmp/zfs-debs" ] && [ "$(ls -A /tmp/zfs-debs/*.deb 2>/dev/null)" ]; then
  echo "Using cached OpenZFS packages..."
  # Install packages (excluding dkms and dracut, but including kmod)
  sudo apt-get -y install $(find /tmp/zfs-debs -name '*.deb' | grep -Ev 'dkms|dracut')
  echo "Updating module dependencies..."
  sudo depmod -a
else
  # Build and install OpenZFS from source
  echo "Building OpenZFS from source..."
  cd /tmp
  git clone --depth 1 --branch truenas/zfs-2.4-release https://github.com/truenas/zfs.git
  cd zfs
  # Run autogen
  ./autogen.sh
  # Configure
  ./configure --prefix=/usr --enable-pyzfs --enable-debuginfo
  # Build native debian packages
  make -j$(nproc) native-deb-kmod native-deb-utils

  # Save the built packages for caching (exclude dkms and dracut)
  echo "Saving built packages for caching..."
  mkdir -p /tmp/zfs-debs
  find /tmp -maxdepth 1 -name '*.deb' | grep -Ev 'dkms|dracut' | while read deb; do
    cp "$deb" /tmp/zfs-debs/
  done

  # Install packages (excluding dkms and dracut, but including kmod)
  sudo apt-get -y install $(find /tmp -maxdepth 1 -name '*.deb' | grep -Ev 'dkms|dracut')
  # Run depmod to update module dependencies
  echo "Updating module dependencies..."
  sudo depmod -a
fi

# Build truenas_pylibzfs
echo "Building truenas_pylibzfs..."
cd ~/truenas_pylibzfs
dpkg-buildpackage -us -uc -b

# Install truenas_pylibzfs package
echo "Installing truenas_pylibzfs..."
sudo dpkg -i ../python3-truenas-pylibzfs_*.deb

# Note: We cannot verify the module import here because it requires
# the ZFS kernel module to be loaded, which only happens after reboot
echo "Package installation complete!"
echo "Module verification will happen after VM restart when kernel modules are loaded."
REMOTE_SCRIPT

# Copy ZFS packages back from VM to host for caching (if we built them)
# Do this BEFORE powering off the VM
if [ "$CACHED_ZFS" = "false" ]; then
  echo "Copying built OpenZFS packages from VM for caching..."
  mkdir -p /tmp/zfs-debs
  rsync -az debian@$VM_IP:/tmp/zfs-debs/ /tmp/zfs-debs/ || echo "Note: No packages to cache"
fi

# Clean cloud-init and poweroff VM (like ZFS workflow does)
echo "Cleaning cloud-init and powering off VM..."
ssh debian@$VM_IP 'sudo cloud-init clean --logs && sync && sleep 2 && sudo poweroff' &

echo "Build complete, VM shutting down for restart"
