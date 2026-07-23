#!/usr/bin/env bash

######################################################################
# Map a git ref (branch name) to the TrueNAS train whose rolling
# <train>-nightly kernel and OpenZFS deb releases this repo builds and
# tests against.
#
#   train-for-ref.sh REF
#
# REF - branch name, e.g. master, stable/26, or a pull request base ref.
#
# Prints the train name (master or 26) on stdout.  This is the single
# source of truth for the branch -> train mapping, mirroring the mapping
# truenas/zfs uses for its own branches (truenas/zfs-2.4-release ->
# master, stable/26 -> 26): this repo's master tracks that ZFS 2.4
# release branch, hence the master train.
######################################################################

set -eu

REF="${1:-}"

case "$REF" in
  stable/26) echo "26" ;;
  *)         echo "master" ;;
esac
