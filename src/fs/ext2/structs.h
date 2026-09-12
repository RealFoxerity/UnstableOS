#ifndef FS_EXT2_STRUCTS_H
#define FS_EXT2_STRUCTS_H

#include <stdint.h>

#define dkprintf(fmt, ...) kprintf("ext2: "fmt, ##__VA_ARGS__)

#define EXT2_MAGIC (0xef53)

#define EXT2_FS_CLEAN 1
#define EXT2_FS_ERROR 2

#define EXT2_FS_ERR_IGNORE 1
#define EXT2_FS_ERR_REMOUNT_RO 2
#define EXT2_FS_ERR_PANIC 3

#define EXT2_MIN_BLOCKS 200
#define EXT2_MIN_INODES 20

// highest powers of 3, 5, 7, under UINT32_MAX
// for use in the sparse superblock option
#define EXT2_SPARSE_MOD3 3486784401
#define EXT2_SPARSE_MOD5 1220703125
#define EXT2_SPARSE_MOD7 1977326743

// absolute bare minimum
// superblock backup, the descriptor, block bitmap, inode bitmap, inode table, free block
//#define EXT2_MIN_BLOCK_PER_GROUP 6

// much more reasonable limit
// for context, mkfs.ext2 does 8192
#define EXT2_MIN_BLOCK_PER_GROUP 64

#define EXT2_MAX_SHIFT 8 // 262K

// TODO: implement fsck
struct ext2_sb {
    uint32_t total_inodes;
    uint32_t total_blocks;
    uint32_t reserved_blocks; // TODO
    uint32_t free_blocks;
    uint32_t free_inodes;
    uint32_t superblock_block;
    uint32_t block_size_shift; // size = 1024 << bss
    uint32_t fragment_size_shift; // size = 1024 << fss
    uint32_t blocks_per_group;
    uint32_t fragments_per_group;
    uint32_t inodes_per_group;
    int32_t mount_time;
    int32_t written_time;
    uint16_t mounts_since_fsck;
    uint16_t mounts_per_fsck;
    uint16_t magic;
    uint16_t fs_state; // FS_CLEAN/FS_ERROR
    uint16_t fs_error_handling; // FS_ERR_*
    uint16_t version_minor;
    int32_t last_fsck_time;
    uint32_t fsck_interval;
    uint32_t os_id; // 0 = linux, 1 = hurd, 2 = masix, 3 = freebsd, 4 = lites
    uint32_t version_major;
    uint16_t resv_uid; // TODO
    uint16_t resv_gid; // TODO

    // following if version_major >= 1
    uint32_t first_nonresv_inode; // first user usable inode, 11 in <1.0
    uint16_t inode_size; // 128 in <1.0
    uint16_t owning_bgroup; // if this is a superblock copy, which block group is it a part of
    struct {
        uint32_t prealloc_dir : 1; // TODO
        uint32_t imagic_inodes: 1; // no clue, osdev wiki says AFS nodes
        uint32_t journal      : 1; // EXT3
        uint32_t xattr        : 1; // TODO
        uint32_t resize_inode : 1; // nonstandard inode size used, osdev wiki says resizable fs
        uint32_t dir_hash     : 1; // TODO
    } optional_features;
    struct {
        uint32_t compression  : 1; // won't support
        uint32_t filetype     : 1; // directories use the file_type field
        uint32_t journal_replay:1; // EXT3
        uint32_t journal_device:1; // EXT3
        uint32_t meta_bgroup  : 1; // what?
        uint32_t unknown      : 27;
    } required_features;
    struct {
        uint32_t sparse_sb    : 1; // backup superblocks only on 0, 1, and multiples of 3/5/7
        uint32_t large_files  : 1; // 64 bit file sizes
        uint32_t dir_btree    : 1; // TODO
        uint32_t unknown      : 29;
    } required_rw_features;
    uint8_t uuid[16];
    char label[16];
    char last_mountpoint[64];
    uint32_t compression; // 1 = LZV1, 2 = LZRW3, 4 = GZIP, 8 = BZIP2, 16 = LZO
    // performance hints, TODO
    uint8_t prealloc_blocks;
    uint8_t prealloc_dir_blocks;
    // journal support, ext3 and newer, TODO when implementing EXT3
    uint8_t journal_uuid[16];
    uint32_t journal_inode;
    uint32_t journal_device;
    uint32_t orphan_list_head;
    // directory indexing, if the optional feature dir_hash is set
    uint32_t hash_seed[4];
    uint8_t default_hash_version;
    // other options
    uint32_t default_mount_options;
    uint32_t first_meta_bgroup; // what?

    // not necessary to be here, but i want to do math with sizeof(ext2_sb)
    uint8_t __unused[760];
} __attribute__((aligned(4)));

struct ext2_bgroup_desc {
    uint32_t block_bitmap_block; // block id with the block bitmap of this block group
    uint32_t inode_bitmap_block;  //               ... inode ...
    uint32_t inode_table_block;   // block id with the first inode of the inode table
    uint16_t free_blocks;
    uint16_t free_inodes;
    uint16_t used_dir_inodes; // why?
    uint32_t __unused[3];
} __attribute__((aligned(4)));


#define EXT2_BAD_INO            1
#define EXT2_ROOT_INO           2
//#define EXT2_ACL_IDX_INO        3
//#define EXT2_ACL_DATA_INO       4
#define EXT2_BOOT_LOADER_INO    5
#define EXT2_UNDEL_DIR_INO      6

struct ext2_inode {
    uint16_t mode;
    uint16_t uid;
    union {
        int32_t size; // strictly technicall a
        uint32_t lo_size;
    };
    int32_t atime;
    int32_t btime; // birth/creation time, btime to avoid confusion with ctime in inode_t
    int32_t mtime;
    int32_t dtime; // deletion time
    uint16_t gid;
    uint16_t nlink;
    uint32_t used_512blocks; // needs recalculation to proper blocks
    struct {
        uint32_t secure_deletion : 1; // won't support, random data written before unlink
        uint32_t for_undelete    : 1; // won't support, instead move to the trash bin (basically)
        uint32_t compressed      : 1; // won't support
        uint32_t sync            : 1; // TODO
        uint32_t immutable       : 1; // irrelevant as we don't do defragmentation
        uint32_t append          : 1; // TODO
        uint32_t nodump          : 1; // won't support, even if nlink is 0, won't be deleted, why?
        uint32_t noatime         : 1;
        // for compression
        uint32_t dirty           : 1; // won't support
        uint32_t comp_blocks     : 1; // won't support
        uint32_t no_comp         : 1; // won't support
        uint32_t comp_error      : 1; // won't support
        // end of compression
        uint32_t accelerated_dir : 1; // TODO: btree or hash
        uint32_t imagic          : 1; // "AFS directory"?, won't support
        uint32_t journal_data    : 1; // TODO when implementing EXT3
    } flags;
    uint32_t osd1; // os dependant
    uint32_t blocks[12]; // TODO: cache these, rbtree would be great
    uint32_t indirect_block;
    uint32_t d_indirect_block;
    uint32_t t_indirect_block;
    uint32_t nfs_generation;
    uint32_t file_acl;
    union {
        uint32_t dir_acl;
        int32_t hi_size;
    };
    uint32_t fragment; // basically unused and unsupported everywhere
    union {
        struct {
            uint8_t fragid; // unused
            uint8_t fsize;  // unused
            uint16_t mode_high; // unused
            uint16_t uid_high;
            uint16_t gid_high;
            uint32_t author;    // unused
        } osd2_hurd;
        struct {
            uint8_t fragid; // unused
            uint8_t fsize;  // unused
            uint16_t : 16;
            uint16_t uid_high;
            uint16_t gid_high;
            uint32_t : 32;
        } osd2_linux;
    };
} __attribute__((aligned(4)));

#define EXT2_FT_UNK 0
#define EXT2_FT_REG 1
#define EXT2_FT_DIR 2
#define EXT2_FT_CHR 3
#define EXT2_FT_BLK 4
#define EXT2_FT_FIFO 5
#define EXT2_FT_SOCK 6
#define EXT2_FT_LNK 7

struct ext2_directory {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t name_len;
    uint8_t file_type;
    /* 0 - 255 byte name here */
} __attribute__((aligned(4)));


#define EXT2_LOOKASIDE_BUFFER_LEN 128
#define EXT2_LAST_BUFFER_LEN 64

#include "../../include/kernel_spinlock.h"
struct ext2_metadata {
    rw_spinlock_t access_lock;
    struct ext2_sb sb;
    // fields for easier access
    uint32_t block_size;
    uint32_t inode_size;
    uint32_t block_groups;
    unsigned char sparse_sb : 1;
    unsigned char large_files : 1;
    unsigned char filetype : 1;

    unsigned long last_visited_bgroup; // will go up the disk and then back to 0
    unsigned long lookaside_cache_inodes[EXT2_LOOKASIDE_BUFFER_LEN];
    unsigned long lookaside_cache_blocks[EXT2_LOOKASIDE_BUFFER_LEN];
    unsigned long lookaside_cache_inodes_tail;
    unsigned long lookaside_cache_blocks_tail;

    unsigned long last_freed_inodes[EXT2_LAST_BUFFER_LEN]; // strictly 0 indexed
    unsigned long last_freed_blocks[EXT2_LAST_BUFFER_LEN]; // strictly 0 indexed
    unsigned long last_freed_inodes_tail;
    unsigned long last_freed_blocks_tail;
};

int ext2_replenish_cache(superblock_t * sb, char for_inodes);
unsigned long ext2_allocate(superblock_t * sb, ino_t ideal_locality, char get_inode);
unsigned long ext2_get_block(superblock_t * sb, struct ext2_inode * inode, off_t target_offset, char is_blockno, char alloc);
void ext2_adjust_bgroup_dir_count(superblock_t * sb, unsigned long ino, short delta);
void ext2_free(superblock_t * sb, unsigned long block, char get_inode);
int ext2_free_indirect(superblock_t * sb, unsigned long block, unsigned long left);
int ext2_free_doubly_indirect(superblock_t * sb, unsigned long block, unsigned long left);
int ext2_free_triply_indirect(superblock_t * sb, unsigned long block, unsigned long left);
int ext2_alloc_dentry(superblock_t * sb, struct ext2_inode * dir, const char * name, ino_t ino, mode_t file_type);

#endif