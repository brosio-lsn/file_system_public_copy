/**
 * @file mount.c
 * @brief mounts the filesystem
 *
 * @author Ludovic Mermod / Aurélien Soccard / Edouard Bugnion
 * @date 2022
 */

#include <string.h> // memset()
#include <inttypes.h>

#include "error.h"
#include "mount.h"
#include "sector.h"
#include "util.h" // for TO_BE_IMPLEMENTED() -- remove later
#include "bmblock.h"
#include "inode.h"
#include "stdlib.h"

#define MEDIUM_FILESYS_SIZE (7*256)
int write_sector_on_bitmap(const struct inode * inode, struct unix_filesystem *u);
int fill_bitmaps(struct unix_filesystem *u);


int mountv6(const char *filename, struct unix_filesystem *u)
{
    M_REQUIRE_NON_NULL(filename);
    M_REQUIRE_NON_NULL(u);
    memset(u, 0, sizeof(*u));
    FILE *fichier = fopen(filename, "rb+");
    if (fichier == NULL) {
        return ERR_IO;
    }
    u->f = fichier;
    uint8_t table[SECTOR_SIZE];
    int s_read_bootblock_sector = sector_read(u->f, BOOTBLOCK_SECTOR, (void *) table);
    if (s_read_bootblock_sector != ERR_NONE) {
        fclose(u->f);
        return s_read_bootblock_sector;
    }
    if (table[BOOTBLOCK_MAGIC_NUM_OFFSET] != BOOTBLOCK_MAGIC_NUM) {
        fclose(u->f);
        return ERR_BAD_BOOT_SECTOR;
    }
    int s_read_superblock = sector_read(u->f, SUPERBLOCK_SECTOR, (void *) &(u->s));
    if (s_read_superblock != ERR_NONE) {
        fclose(u->f);
        return s_read_superblock;
    }

    //bitmap allocation
    //inode
    struct bmblock_array *ibm;
    uint64_t min_inode = ROOT_INUMBER;
    uint64_t max_inode = u->s.s_isize * INODES_PER_SECTOR;
    ibm = bm_alloc(min_inode, max_inode);
    if (ibm == NULL) {
        fclose(u->f);
        return ERR_NOMEM;
    }
    //sectors
    struct bmblock_array *fbm;
    uint64_t min_sector = u->s.s_block_start;
    uint64_t max_sector = u->s.s_fsize;
    fbm = bm_alloc(min_sector, max_sector);
    if (fbm == NULL) {
        free(ibm);
        fclose(u->f);
        return ERR_NOMEM;
    }
    u->ibm = ibm;
    u->fbm = fbm;
    int fill = fill_bitmaps(u);
    if (fill != ERR_NONE) {
        free(ibm);
        free(fbm);
        u->ibm = NULL;
        u->fbm = NULL;
        return fill;
    }
    return ERR_NONE;
}

int fill_bitmaps(struct unix_filesystem *u)
{
    M_REQUIRE_NON_NULL(u);
    size_t inode_sector_start = u->s.s_inode_start;
    size_t inode_sector_end = u->s.s_isize + inode_sector_start-1;
    for (size_t i = inode_sector_start; i < inode_sector_end; i += 1) {
        //flag for setting a 1 or a 0 in sector bitmap
        int found_a_used_inode = 0;
        for (size_t j = 0; j < INODES_PER_SECTOR; j++) {
            uint16_t inr = INODES_PER_SECTOR * (i - inode_sector_start) + j + ROOT_INUMBER;
            struct inode myInode = {0};
            int ret = inode_read(u, inr, &myInode);
            if (ret != ERR_UNALLOCATED_INODE) {
                bm_set(u->ibm, inr);
                found_a_used_inode = 1;
            }
            if(ret==ERR_NONE) {
                int write = write_sector_on_bitmap(&myInode, u);
                if (write != 0) {
                    return write;
                }
            }//fix in response of the lost point from last submission (cf. professor)
            if (ret != ERR_NONE && ret != ERR_UNALLOCATED_INODE) {
                return ret;
            }
        }
        //to avoid repetition
        if(found_a_used_inode) {
            bm_set(u->fbm, i);
        }
    }
    return ERR_NONE;
}


int write_sector_on_bitmap(const struct inode * inode, struct unix_filesystem *u)
{
    M_REQUIRE_NON_NULL(inode);
    M_REQUIRE_NON_NULL(u);
    //size of the file in sectors
    size_t size_tot = inode_getsize(inode)/SECTOR_SIZE + 1;
    if (size_tot <= MEDIUM_FILESYS_SIZE) { //if file is too big, the code below will report an error
        size_t i_addr_max_index = size_tot/ADDRESSES_PER_SECTOR;
        for(size_t index_i_addr =0; index_i_addr<=i_addr_max_index; ++index_i_addr) {
            bm_set(u->fbm, inode->i_addr[index_i_addr]);
        }
    }
    int32_t sector_index = 0;
    int s_index_inode =  inode_findsector(u, inode, sector_index);
    while (s_index_inode>=0) {
        bm_set(u->fbm, s_index_inode);
        s_index_inode = inode_findsector(u, inode, sector_index);
        ++sector_index;
    }
    if(s_index_inode!=ERR_OFFSET_OUT_OF_RANGE) {
        return s_index_inode;
    }
    return ERR_NONE;
}




int umountv6(struct unix_filesystem *u)
{
    M_REQUIRE_NON_NULL(u);
    if (u->f == NULL) {
        return ERR_IO;
    }
    int close = fclose(u->f);
    if(close !=0) {
        return ERR_IO;
    }
    //unallocating the bitmap vectors
    if (u->ibm != NULL) {
        free(u->ibm);
    }
    if (u->fbm != NULL) {
        free(u->fbm);
    }
    memset(u, 0, sizeof(struct unix_filesystem));
    return ERR_NONE;
}