/* ZFS definitions used by the user space component of the
 * grey box corruption tool.
 */


#include <sys/types.h>

#define VDEV_SKIP_SIZE          (8 << 10)
#define VDEV_BOOT_HEADER_SIZE   (8 << 10)
#define VDEV_PHYS_SIZE          (112 << 10)
#define VDEV_UBERBLOCK_RING     (128 << 10)

#define SIZE_RAMDISK 64*1048576
#define UBSIZE 1024
#define UBERBLOCK_MAGIC         0x00bab10c
#define UBERBLOCK_SHIFT         10

#define MATCH_BITS      6
#define MATCH_MIN       3
#define MATCH_MAX       ((1 << MATCH_BITS) + (MATCH_MIN - 1))
#define OFFSET_MASK     ((1 << (16 - MATCH_BITS)) - 1)
#define LEMPEL_SIZE     256
#ifndef NBBY
#define NBBY		8
#endif
// sys/spa.h
typedef struct zio_cksum
{
  uint64_t zc_word[4];
} zio_cksum_t;

// sys/zio.h 

typedef struct zio_block_tail
{
  uint64_t zbt_magic;		/* for validation, endianness   */
  zio_cksum_t zbt_cksum;	/* 256-bit checksum             */
} zio_block_tail_t;

//vdev_impl.h
typedef struct vdev_phys
{
  char vp_nvlist[VDEV_PHYS_SIZE - sizeof (zio_block_tail_t)];
  zio_block_tail_t vp_zbt;
}
vdev_phys_t;

typedef struct vdev_boot_header
{
  uint64_t vb_magic;		/* VDEV_BOOT_MAGIC      */
  uint64_t vb_version;		/* VDEV_BOOT_VERSION    */
  uint64_t vb_offset;		/* start offset (bytes) */
  uint64_t vb_size;		/* size (bytes)         */
  char vb_pad[VDEV_BOOT_HEADER_SIZE - 4 * sizeof (uint64_t)];
}
vdev_boot_header_t;

typedef struct vdev_label
{
  char vl_pad[VDEV_SKIP_SIZE];	/*   8K */
  vdev_boot_header_t vl_boot_header;	/*   8K */
  vdev_phys_t vl_vdev_phys;	/* 112K */
  char vl_uberblock[VDEV_UBERBLOCK_RING];	/* 128K */
} vdev_label_t;			/* 256K total */



typedef struct dva
{
  uint64_t dva_word[2];
} dva_t;

typedef struct blkptr
{
  dva_t blk_dva[3];		// 128-bit Data Virtual Address /
  uint64_t blk_prop;		// size, compression, type, etc /
  uint64_t blk_pad[3];		// Extra space for the future   /
  uint64_t blk_birth;		// transaction group at birth   /
  uint64_t blk_fill;		// fill count                   /
  zio_cksum_t blk_cksum;	// 256-bit checksum             /
} blkptr_t;

struct uberblock
{
  uint64_t ub_magic;		// UBERBLOCK_MAGIC              /
  uint64_t ub_version;		// SPA_VERSION                  /
  uint64_t ub_txg;		// txg of last sync             /
  uint64_t ub_guid_sum;		// sum of all vdev guids        /
  uint64_t ub_timestamp;	// UTC time of last sync        /
  blkptr_t ub_rootbp;		// MOS objset_phys_t            /
};




// object set
#define DNODE_SHIFT             9	/* 512 bytes */
#define DNODE_SIZE      (1 << DNODE_SHIFT)
#define DNODE_CORE_SIZE         64	/* 64 bytes for dnode sans blkptrs */
#define SPA_BLKPTRSHIFT 7	/* blkptr_t is 128 bytes        */
#define DN_MAX_BONUSLEN (DNODE_SIZE - DNODE_CORE_SIZE - (1 << SPA_BLKPTRSHIFT))


// ZIL
typedef struct zil_header {
       uint64_t zh_claim_txg;  // txg in which log blocks were claimed /
       uint64_t zh_replay_seq; // highest replayed sequence number /
       blkptr_t zh_log;        // log chain /
       uint64_t zh_claim_seq;  // highest claimed sequence number 
       uint64_t zh_pad[5];
} zil_header_t;

typedef struct {                        // common log record header /
       uint64_t        lrc_txtype;     // intent log transaction type /
       uint64_t        lrc_reclen;     // transaction record length /
       uint64_t        lrc_txg;        // dmu transaction group number /
       uint64_t        lrc_seq;        // see comment above 
} lr_t;

typedef struct dnode_phys
{
  uint8_t dn_type;		// dmu_object_type_t /
  uint8_t dn_indblkshift;	// ln2(indirect block size) /
  uint8_t dn_nlevels;		// 1=dn_blkptr->data blocks /
  uint8_t dn_nblkptr;		// length of dn_blkptr /
  uint8_t dn_bonustype;		// type of data in bonus buffer /
  uint8_t dn_checksum;		// ZIO_CHECKSUM type /
  uint8_t dn_compress;		// ZIO_COMPRESS type /
  uint8_t dn_flags;		// DNODE_FLAG_* /
  uint16_t dn_datablkszsec;	// data block size in 512b sectors /
  uint16_t dn_bonuslen;		// length of dn_bonus /
  uint8_t dn_pad2[4];

  // accounting is protected by dn_dirty_mtx /
  uint64_t dn_maxblkid;		// largest allocated block ID /
  uint64_t dn_used;		// bytes (or sectors) of disk space */

  uint64_t dn_pad3[4];

  blkptr_t dn_blkptr[1];
  uint8_t dn_bonus[DN_MAX_BONUSLEN];
} dnode_phys_t;



typedef struct objset_phys
{
  dnode_phys_t os_meta_dnode;
  zil_header_t os_zil_header;
  uint64_t os_type;
  char os_pad[1024 - sizeof (dnode_phys_t) - sizeof (zil_header_t) -
	      sizeof (uint64_t)];
}
objset_phys_t;


// ZAP Object
 
#define MZAP_ENT_LEN            64
#define MZAP_NAME_LEN           (MZAP_ENT_LEN - 8 - 4 - 2)

typedef struct mzap_ent_phys
{
  uint64_t mze_value;
  uint32_t mze_cd;
  uint16_t mze_pad;		/* in case we want to chain them someday */
  char mze_name[MZAP_NAME_LEN];
} mzap_ent_phys_t;

typedef struct mzap_phys
{
  uint64_t mz_block_type;	/* ZBT_MICRO */
  uint64_t mz_salt;
  uint64_t mz_normflags;
  uint64_t mz_pad[5];
  mzap_ent_phys_t mz_chunk[1];
  /* actually variable size depending on block size */
} mzap_phys_t;

// DSL directory


typedef struct dsl_dir_phys {
  uint64_t dd_creation_time; /* not actually used */
  uint64_t dd_head_dataset_obj;
  uint64_t dd_parent_obj;
  uint64_t dd_clone_parent_obj;
  uint64_t dd_child_dir_zapobj;
/*
* how much space our children are accounting for; for leaf
* datasets, == physical space used by fs + snaps
*/
  uint64_t dd_used_bytes;
  uint64_t dd_compressed_bytes;
  uint64_t dd_uncompressed_bytes;
/* Administrative quota setting */
  uint64_t dd_quota;
/* Administrative reservation setting */
  uint64_t dd_reserved;
  uint64_t dd_props_zapobj;
  uint64_t dd_deleg_zapobj;	/* dataset permissions */
  uint64_t dd_pad[20]; /* pad out to 256 bytes for good measure */
} dsl_dir_phys_t;


typedef struct dsl_dataset_phys {
uint64_t ds_dir_obj;
uint64_t ds_prev_snap_obj;
uint64_t ds_prev_snap_txg;
uint64_t ds_next_snap_obj;
uint64_t ds_snapnames_zapobj;	/* zap obj of snaps; ==0 for snaps */
uint64_t ds_num_children;	/* clone/snap children; ==0 for head */
uint64_t ds_creation_time;	/* seconds since 1970 */
uint64_t ds_creation_txg;
uint64_t ds_deadlist_obj;
uint64_t ds_used_bytes;
uint64_t ds_compressed_bytes;
uint64_t ds_uncompressed_bytes;
uint64_t ds_unique_bytes;	/* only relevant to snapshots */
/*
 * The ds_fsid_guid is a 56-bit ID that can change to avoid
 * collisions.  The ds_guid is a 64-bit ID that will never
 * change, so there is a small probability that it will collide.
 */
uint64_t ds_fsid_guid;
uint64_t ds_guid;
uint64_t ds_flags;
blkptr_t ds_bp;
uint64_t ds_pad[8]; /* pad out to 320 bytes for good measure */
} dsl_dataset_phys_t; 

typedef struct corrupt {
        char            *ptr_name;       /* The pointer getting squished   */
        uint64_t        new_value;      /* New value of the pointer.        */
        uint32_t        blockno;        /* Block where the pointer resides. */
        uint32_t        offset;           /* Offset */
        uint32_t        bit;          /* bit */
        struct corrupt_t *     nxt;
} corrupt_t;
 
