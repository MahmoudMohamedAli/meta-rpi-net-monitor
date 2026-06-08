#!/bin/bash
# =============================================================================
# update-nfs-rootfs.sh
# Updates the NFS rootfs from the latest Yocto build output.
# Usage: ./update-nfs-rootfs.sh
# =============================================================================

set -euo pipefail

# ── Configuration ─────────────────────────────────────────────────────────────
NFS_DIR="/srv/nfs/rpi"
BACKUP_DIR="/srv/nfs/rpi_backup"
DEPLOY_DIR="/home/mahmoud/Desktop/yocto/poky/build/tmp/deploy/images/raspberrypi3-64"
IMAGE_NAME="core-image-minimal-raspberrypi3-64.tar.bz2"
TARBALL="$DEPLOY_DIR/$IMAGE_NAME"

# ── Colors ────────────────────────────────────────────────────────────────────
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # no color

log()  { echo -e "${GREEN}[+]${NC} $1"; }
warn() { echo -e "${YELLOW}[!]${NC} $1"; }
die()  { echo -e "${RED}[ERROR]${NC} $1"; exit 1; }

# ── Checks ────────────────────────────────────────────────────────────────────
log "Checking prerequisites..."

[[ "$EUID" -ne 0 ]] && die "Run as root: sudo $0"

[[ -f "$TARBALL" ]] || die "Rootfs tarball not found at: $TARBALL"

[[ -d "$NFS_DIR" ]] || die "NFS directory not found at: $NFS_DIR"

# ── Backup ────────────────────────────────────────────────────────────────────
if [[ -d "$BACKUP_DIR" ]]; then
    warn "Removing old backup at $BACKUP_DIR"
    rm -rf "$BACKUP_DIR"
fi

log "Backing up current rootfs to $BACKUP_DIR ..."
mv "$NFS_DIR" "$BACKUP_DIR"

# ── Extract ───────────────────────────────────────────────────────────────────
log "Creating fresh NFS directory..."
mkdir -p "$NFS_DIR"

log "Extracting new rootfs (this may take a moment)..."
tar -xjf "$TARBALL" -C "$NFS_DIR"

# ── Permissions ───────────────────────────────────────────────────────────────
log "Fixing ownership..."
chown -R root:root "$NFS_DIR"

# ── Re-export ─────────────────────────────────────────────────────────────────
log "Re-exporting NFS shares..."
exportfs -ra

# ── Done ──────────────────────────────────────────────────────────────────────
echo ""
log "Rootfs updated successfully!"
echo -e "  ${GREEN}New rootfs :${NC} $NFS_DIR"
echo -e "  ${GREEN}Backup at  :${NC} $BACKUP_DIR"
echo ""
warn "Power cycle your Raspberry Pi to boot the new image."