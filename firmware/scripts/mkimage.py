#!/usr/bin/env python3
"""
Build a minimal, read-only ext2 image containing:

    /grub-boot-switch/sel00      (content: "00\n")

The filename digits and the file-content digits are the ONLY bytes that ever
change at runtime. This script locates their exact byte offsets and emits a C
header (ext2_image.h) with the image blob plus those offsets, so the firmware
can overlay the two GPIO-derived digits without any ext2 code on the MCU.

ext2 has no metadata checksums (unlike ext4 with metadata_csum), so overlaying
those bytes in the served sector stream produces a perfectly valid filesystem
for whatever N the switch encodes.

Block size 1024. One block group. Layout (block -> use):
  0  boot block (unused padding)
  1  superblock
  2  group descriptor table
  3  block bitmap
  4  inode bitmap
  5  inode table (inodes 1-8)
  6  inode table (inodes 9-16)
  7  root dir (inode 2) data
  8  /grub-boot-switch (inode 11) data
  9  /grub-boot-switch/sel00 (inode 12) data
"""
import struct, sys, uuid

# Magic filesystem UUID every switch device carries. Must stay in sync with
# SWITCH_UUID in src/config and the ID_FS_UUID match in
# src/99-grub-boot-switch.rules.
SWITCH_UUID      = '67727562-626f-6f74-7377-697463680001'

BS               = 1024      # block size
IMG_BLOCKS       = 10        # total blocks 0..9
INODES           = 16
INODES_PER_GROUP = 16
BLOCKS_PER_GROUP = 8 * BS    # 8192
FIRST_INO        = 11
INODE_SIZE       = 128
NOW              = 0         # fixed timestamps -> reproducible image

# block indices
B_BOOT, B_SUPER, B_GDT, B_BLK_BMP, B_INO_BMP = 0, 1, 2, 3, 4
B_INO_TBL   = 5   # spans blocks 5,6
B_ROOT_DATA = 7
B_GBS_DATA  = 8
B_FILE_DATA = 9

FT_REG, FT_DIR = 1, 2
S_IFDIR, S_IFREG = 0x4000, 0x8000

img = bytearray(BS * IMG_BLOCKS)

def put(off, data):
    img[off:off+len(data)] = data

# ------------------------------------------------------------------ superblock
sb = bytearray(BS)
def sbp(off, fmt, *v): struct.pack_into(fmt, sb, off, *v)

sbp(0,   '<I', INODES)             # s_inodes_count
sbp(4,   '<I', IMG_BLOCKS)         # s_blocks_count
sbp(8,   '<I', 0)                  # s_r_blocks_count
sbp(12,  '<I', 0)                  # s_free_blocks_count
sbp(16,  '<I', INODES - 12)        # s_free_inodes_count (inodes 1..12 used)
sbp(20,  '<I', 1)                  # s_first_data_block
sbp(24,  '<I', 0)                  # s_log_block_size  (0 => 1024)
sbp(28,  '<I', 0)                  # s_log_frag_size
sbp(32,  '<I', BLOCKS_PER_GROUP)   # s_blocks_per_group
sbp(36,  '<I', BLOCKS_PER_GROUP)   # s_frags_per_group
sbp(40,  '<I', INODES_PER_GROUP)   # s_inodes_per_group
sbp(44,  '<I', NOW)                # s_mtime
sbp(48,  '<I', NOW)                # s_wtime
sbp(52,  '<H', 0)                  # s_mnt_count
sbp(54,  '<H', 0xFFFF)            # s_max_mnt_count (-1)
sbp(56,  '<H', 0xEF53)            # s_magic
sbp(58,  '<H', 1)                  # s_state (clean)
sbp(60,  '<H', 1)                  # s_errors (continue)
sbp(62,  '<H', 0)                  # s_minor_rev_level
sbp(64,  '<I', NOW)                # s_lastcheck
sbp(68,  '<I', 0)                  # s_checkinterval
sbp(72,  '<I', 0)                  # s_creator_os (Linux)
sbp(76,  '<I', 1)                  # s_rev_level (DYNAMIC)
sbp(80,  '<H', 0)                  # s_def_resuid
sbp(82,  '<H', 0)                  # s_def_resgid
sbp(84,  '<I', FIRST_INO)          # s_first_ino
sbp(88,  '<H', INODE_SIZE)         # s_inode_size
sbp(90,  '<H', 0)                  # s_block_group_nr
sbp(92,  '<I', 0)                  # s_feature_compat
sbp(96,  '<I', 0x0002)            # s_feature_incompat = FILETYPE
sbp(100, '<I', 0)                  # s_feature_ro_compat
sb[104:120] = uuid.UUID(SWITCH_UUID).bytes                       # s_uuid
sb[120:136] = b'gbswitch'.ljust(16, b'\x00')                    # s_volume_name
put(B_SUPER * BS, sb)

# ---------------------------------------------------- block group descriptor
gd = bytearray(32)
struct.pack_into('<I', gd, 0, B_BLK_BMP)     # bg_block_bitmap
struct.pack_into('<I', gd, 4, B_INO_BMP)     # bg_inode_bitmap
struct.pack_into('<I', gd, 8, B_INO_TBL)     # bg_inode_table
struct.pack_into('<H', gd, 12, 0)            # bg_free_blocks_count
struct.pack_into('<H', gd, 14, INODES - 12)  # bg_free_inodes_count
struct.pack_into('<H', gd, 16, 2)            # bg_used_dirs_count
put(B_GDT * BS, gd)

# ---------------------------------------------------------------- bitmaps
put(B_BLK_BMP * BS, b'\xFF' * BS)            # every block slot marked used
ibmp = bytearray(b'\xFF' * BS)               # start all used
for ino in (13, 14, 15, 16):                 # free the unused inodes
    bit = ino - 1
    ibmp[bit // 8] &= ~(1 << (bit % 8))
put(B_INO_BMP * BS, ibmp)

# ---------------------------------------------------------------- inodes
def write_inode(num, mode, size, links, block0):
    off = B_INO_TBL * BS + (num - 1) * INODE_SIZE
    ino = bytearray(INODE_SIZE)
    struct.pack_into('<H', ino, 0,  mode)     # i_mode
    struct.pack_into('<H', ino, 2,  0)        # i_uid
    struct.pack_into('<I', ino, 4,  size)     # i_size
    struct.pack_into('<I', ino, 8,  NOW)      # i_atime
    struct.pack_into('<I', ino, 12, NOW)      # i_ctime
    struct.pack_into('<I', ino, 16, NOW)      # i_mtime
    struct.pack_into('<I', ino, 20, 0)        # i_dtime
    struct.pack_into('<H', ino, 24, 0)        # i_gid
    struct.pack_into('<H', ino, 26, links)    # i_links_count
    struct.pack_into('<I', ino, 28, 2)        # i_blocks (512B sectors: 1 x 1KB)
    struct.pack_into('<I', ino, 32, 0)        # i_flags
    struct.pack_into('<I', ino, 40, block0)   # i_block[0]
    put(off, ino)

write_inode(2,  S_IFDIR | 0o755, BS, 3, B_ROOT_DATA)   # /
write_inode(11, S_IFDIR | 0o755, BS, 2, B_GBS_DATA)    # /grub-boot-switch
write_inode(12, S_IFREG | 0o644, 3,  1, B_FILE_DATA)   # sel00  (content "00\n")

# ---------------------------------------------------------------- dir blocks
def dirent(inode, name, ftype):
    nb   = name.encode()
    rec  = (8 + len(nb) + 3) & ~3
    e = bytearray(rec)
    struct.pack_into('<I', e, 0, inode)
    struct.pack_into('<H', e, 4, rec)
    e[6] = len(nb)
    e[7] = ftype
    e[8:8+len(nb)] = nb
    return e

def dir_block(entries):
    parts = [dirent(*e) for e in entries]
    start = sum(len(p) for p in parts[:-1])   # offset of last entry
    blk = bytearray().join(parts)
    struct.pack_into('<H', blk, start + 4, BS - start)  # last rec_len fills block
    return blk + bytearray(BS - len(blk))

put(B_ROOT_DATA * BS, dir_block([
    (2,  '.',                FT_DIR),
    (2,  '..',               FT_DIR),
    (11, 'grub-boot-switch', FT_DIR),
]))
put(B_GBS_DATA * BS, dir_block([
    (11, '.',     FT_DIR),
    (2,  '..',    FT_DIR),
    (12, 'sel00', FT_REG),
]))

# ---------------------------------------------------------------- file data
content = b'00\n'
fb = bytearray(BS)
fb[0:len(content)] = content
put(B_FILE_DATA * BS, fb)

# ---------------------------------------------------- locate dynamic digits
name_off  = img.index(b'sel00')          # 's' of the dir entry name
NAME_TENS = name_off + 3
NAME_ONES = name_off + 4
CONT_TENS = B_FILE_DATA * BS + 0
CONT_ONES = B_FILE_DATA * BS + 1
assert img[NAME_TENS:NAME_ONES+1] == b'00'
assert img[CONT_TENS:CONT_ONES+1] == b'00'

# ---------------------------------------------------------------- emit header
with open('ext2_image.h', 'w') as f:
    f.write('// Auto-generated by mkimage.py -- do not edit.\n')
    f.write('#ifndef EXT2_IMAGE_H\n#define EXT2_IMAGE_H\n#include <stdint.h>\n\n')
    f.write(f'#define EXT2_IMAGE_SIZE   {len(img)}u\n')
    f.write(f'#define DISK_BLOCK_SIZE   512u\n')
    f.write(f'#define DISK_BLOCK_COUNT  {len(img)//512}u\n\n')
    f.write(f'#define OFF_NAME_TENS     {NAME_TENS}u\n')
    f.write(f'#define OFF_NAME_ONES     {NAME_ONES}u\n')
    f.write(f'#define OFF_CONT_TENS     {CONT_TENS}u\n')
    f.write(f'#define OFF_CONT_ONES     {CONT_ONES}u\n\n')
    f.write('static const uint8_t ext2_image[EXT2_IMAGE_SIZE] = {\n')
    for i in range(0, len(img), 16):
        row = ','.join(f'0x{b:02x}' for b in img[i:i+16])
        f.write('  ' + row + ',\n')
    f.write('};\n#endif // EXT2_IMAGE_H\n')

with open('grub-boot-switch.img', 'wb') as f:
    f.write(img)

print(f'image size      : {len(img)} bytes ({len(img)//512} x 512B sectors)')
print(f'name digits at  : {NAME_TENS}, {NAME_ONES}')
print(f'content digits  : {CONT_TENS}, {CONT_ONES}')
print('wrote ext2_image.h and grub-boot-switch.img')
