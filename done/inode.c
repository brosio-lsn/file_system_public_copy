#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "error.h"
#include "mount.h"
#include "u6fs_utils.h"
#include "unixv6fs.h"
#include "inode.h"
#include "sector.h"
#define INODE_SIZEB 32
#define SMALL_FILESYS_SIZE 8
#define MEDIUM_FILESYS_SIZE (7*256)
#define BYTE_SHIFT 8




//Prototypes
int inode_read(const struct unix_filesystem *u, uint16_t inr, struct inode *inode);
int inode_scan_print(const struct unix_filesystem *u);
int inode_findsector(const struct unix_filesystem *u, const struct inode *i, int32_t file_sec_off);
int inode_findsector_bigfile(const struct unix_filesystem *u, const struct inode *i, int32_t file_sec_off);

int inode_read(const struct unix_filesystem *u, uint16_t inr, struct inode *inode) {
	M_REQUIRE_NON_NULL(u);
	M_REQUIRE_NON_NULL(inode);
	FILE* f = u->f;

	struct superblock super_block = {0};
	super_block = u->s;
	uint32_t inode_start = super_block.s_inode_start;
	uint32_t target_sector_nb = inr/INODES_PER_SECTOR + inode_start;
	uint32_t sector_end = inode_start + super_block.s_isize -1;

	if (target_sector_nb > sector_end || super_block.s_isize == 0 || inr == 0 ) {
		return ERR_INODE_OUT_OF_RANGE;
	}
	uint8_t sector[SECTOR_SIZE];
	int ret = sector_read(f, target_sector_nb, sector);
	if (ret != 0) {
		return ret;
	}
	size_t inode_index = (inr - inr/INODES_PER_SECTOR * INODES_PER_SECTOR)*INODE_SIZEB;
	memcpy(inode, &sector[inode_index], sizeof(struct inode));
	if (!(inode->i_mode & IALLOC)) {
		return ERR_UNALLOCATED_INODE;
	}
	return ERR_NONE;
	}

int inode_scan_print(const struct unix_filesystem *u){
	M_REQUIRE_NON_NULL(u);
	struct superblock super_block;
	super_block = u->s;
	size_t inode_sector_start = super_block.s_inode_start;
	size_t inode_sector_end = super_block.s_isize + inode_sector_start-1;

	for (size_t i = inode_sector_start; i < inode_sector_end; i+= 1) {
		for (size_t j = 0; j < INODES_PER_SECTOR; j++) {
			uint16_t inr = INODES_PER_SECTOR*(i-inode_sector_start) + j + ROOT_INUMBER;
			struct inode myInode = {0};
			int ret = inode_read(u, inr, &myInode);
			size_t size = inode_getsize(&myInode);
			if (ret == 0) {
				const char * type = myInode.i_mode & IFDIR ? SHORT_DIR_NAME : SHORT_FIL_NAME;
				pps_printf("inode %d (%s) len %zu\n", inr, type, size);
				}
			if (ret != ERR_NONE && ret != ERR_UNALLOCATED_INODE) {
				return ret;
			}
		}
	}
		return ERR_NONE;
}
int inode_findsector(const struct unix_filesystem *u, const struct inode *i, int32_t file_sec_off){
	M_REQUIRE_NON_NULL(u);
	M_REQUIRE_NON_NULL(i);
	if (!(i->i_mode & IALLOC)) {
		return ERR_UNALLOCATED_INODE;
	}
	size_t size_tot = inode_getsize(i)/SECTOR_SIZE + 1;
	if (file_sec_off <0 || file_sec_off >= size_tot) {
		return ERR_OFFSET_OUT_OF_RANGE;
	}
	if (size_tot <= SMALL_FILESYS_SIZE) {
		return i->i_addr[file_sec_off];
	}
	else if (SMALL_FILESYS_SIZE + 1 <= size_tot && size_tot <= MEDIUM_FILESYS_SIZE) {
		return inode_findsector_bigfile(u, i, file_sec_off);
	} else {
		return ERR_FILE_TOO_LARGE;
	}
}
int inode_findsector_bigfile(const struct unix_filesystem *u, const struct inode *i, int32_t file_sec_off) {
	size_t sector_nb = file_sec_off/ADDRESSES_PER_SECTOR;
	size_t index_in_sector = file_sec_off % ADDRESSES_PER_SECTOR;
	uint8_t sector[SECTOR_SIZE];
	int ret = sector_read(u->f, i->i_addr[sector_nb], sector);
	if (ret != 0) {
		return ret;
	}
	int16_t result = 0;
	memcpy(&result, &sector[index_in_sector*2], sizeof(uint16_t));
	return result;
}

int inode_write(struct unix_filesystem *u, uint16_t inr, const struct inode *inode) {
	M_REQUIRE_NON_NULL(u);
	M_REQUIRE_NON_NULL(inode);
	FILE* f = u->f;

	struct superblock super_block = {0};
	super_block = u->s;
	uint32_t inode_start = super_block.s_inode_start;
	uint32_t target_sector_nb = inr/INODES_PER_SECTOR + inode_start;

	uint32_t sector_end = inode_start + super_block.s_isize -1;

	if (target_sector_nb > sector_end || super_block.s_isize == 0 || inr < ROOT_INUMBER) {
		return ERR_INODE_OUT_OF_RANGE;
	}
	uint8_t sector[SECTOR_SIZE];
	int ret = sector_read(f, target_sector_nb, sector);
	if (ret != 0) {
		return ret;
	}
	size_t inode_index = (inr - inr/INODES_PER_SECTOR * INODES_PER_SECTOR)*INODE_SIZEB;
	memcpy(&sector[inode_index], inode, sizeof(struct inode));
	int write = sector_write(f, target_sector_nb, sector);
	if (write != 0) {
		return write;
	}
	return ERR_NONE;
}



int inode_alloc(struct unix_filesystem *u) {
	M_REQUIRE_NON_NULL(u);
	struct bmblock_array* ibm = u->ibm;
	int inr =  bm_find_next(ibm);
	if (inr < 0) {
		return ERR_BITMAP_FULL;
	}
	bm_set(ibm, inr);
	return inr;
}
int inode_setsize(struct inode *inode, int new_size) {
	M_REQUIRE_NON_NULL(inode);
	//if the new_size takes more than 24 bits to represent return bad parameter
	if (new_size < 0 || new_size >> 3*BYTE_SHIFT) return ERR_BAD_PARAMETER;

	//i_size0 represents bits 16-23 of the size of the inode
	inode->i_size0 = (new_size >> 2*BYTE_SHIFT);

	int sixteen_first_bits_mask = (1 << (BYTE_SHIFT*2+1)) -1; 

	//i_size1 represents bits 0-15 of the size of the inode
	inode->i_size1 = new_size & sixteen_first_bits_mask;
	return ERR_NONE;
}


