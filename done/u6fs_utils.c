/**
 * @file u6fs_utils.c
 * @brief Utilities (mostly dump) for UV6 filesystem
 * @author Aurélien Soccard / EB
 * @date 2022
 */

#include <string.h> // memset
#include <inttypes.h>
#include <openssl/sha.h>
#include "mount.h"
#include "sector.h"
#include "error.h"
#include "u6fs_utils.h"
#include "inode.h"
#include "filev6.h"
#include "bmblock.h"

int utils_print_superblock(const struct unix_filesystem *u)
{
    M_REQUIRE_NON_NULL(u);
    pps_printf("**********FS SUPERBLOCK START**********\n");
    pps_printf("%-20s: %" PRIu16 "\n", "s_isize", u->s.s_isize);
    pps_printf("%-20s: %" PRIu16 "\n", "s_fsize", u->s.s_fsize);
    pps_printf("%-20s: %" PRIu16 "\n", "s_fbmsize", u->s.s_fbmsize);
    pps_printf("%-20s: %" PRIu16 "\n", "s_ibmsize", u->s.s_ibmsize);
    pps_printf("%-20s: %" PRIu16 "\n", "s_inode_start", u->s.s_inode_start);
    pps_printf("%-20s: %" PRIu16 "\n", "s_block_start", u->s.s_block_start);
    pps_printf("%-20s: %" PRIu16 "\n", "s_fbm_start", u->s.s_fbm_start);
    pps_printf("%-20s: %" PRIu16 "\n", "s_ibm_start", u->s.s_ibm_start);
    pps_printf("%-20s: %" PRIu8 "\n", "s_flock", u->s.s_flock);
    pps_printf("%-20s: %" PRIu8 "\n", "s_ilock", u->s.s_ilock);
    pps_printf("%-20s: %" PRIu8 "\n", "s_fmod", u->s.s_fmod);
    pps_printf("%-20s: %" PRIu8 "\n", "s_ronly", u->s.s_ronly);
    pps_printf("%-20s: [%" PRIu16 "] %" PRIu16 "\n", "s_time", u->s.s_time[0], u->s.s_time[1]);
    pps_printf("**********FS SUPERBLOCK END**********\n");
    return ERR_NONE;
}

static void utils_print_SHA_buffer(unsigned char *buffer, size_t len)
{
    unsigned char sha[SHA256_DIGEST_LENGTH];
    SHA256(buffer, len, sha);
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        pps_printf("%02x", sha[i]);
    }
    pps_printf("\n");
}

int utils_print_inode(const struct inode *in)
{
    pps_printf("**********FS INODE START**********\n");
    if (in != NULL) {
        pps_printf("i_mode: %hu\n", in->i_mode);
        pps_printf("i_nlink: %hu\n", in->i_nlink);
        pps_printf("i_uid: %hu\n", in->i_uid);
        pps_printf("i_gid: %hu\n", in->i_gid);
        pps_printf("i_size0: %hu\n", in->i_size0);
        pps_printf("i_size1: %hu\n", in->i_size1);
        int size = inode_getsize(in);
        pps_printf("size: %d\n", size);
    } else {
        pps_printf("NULL ptr\n");
    }
    pps_printf("**********FS INODE END************\n");
    return ERR_NONE;
}


int utils_cat_first_sector(const struct unix_filesystem *u, uint16_t inr)
{

    M_REQUIRE_NON_NULL(u);
    struct filev6 f;
    int fileopen = filev6_open(u, inr, &f);
    if (fileopen < 0) {
        pps_printf("filev6_open failed for inode #%d.\n", inr);
        return fileopen;
    }
    pps_printf("\nPrinting inode #%d:\n", inr);
    utils_print_inode(&(f.i_node));
    if (f.i_node.i_mode & IFDIR) {
        pps_printf("which is a directory.\n");
    } else {
        pps_printf("the first sector of data of which contains:\n");
        uint8_t first_sector[SECTOR_SIZE];
        int readblock = filev6_readblock(&f, &first_sector);
        if (readblock < 0) {
            return readblock;
        }
        for (size_t i = 0; i < readblock; ++i) {
            pps_printf("%c", first_sector[i]);
        }
        pps_printf("----\n");
    }
    return ERR_NONE;
}

int utils_print_shafile(const struct unix_filesystem *u, uint16_t inr)
{
    M_REQUIRE_NON_NULL(u);
    struct filev6 fv6 = {0};
    int open = filev6_open(u, inr, &fv6);
    if (open < 0) {
        return open;
    } else {
        struct inode inode = fv6.i_node;
        pps_printf("SHA inode %d: ", inr);
        if (inode.i_mode & IFDIR) {
            pps_printf("%s\n", SHORT_DIR_NAME);
        } else {
            uint8_t buffer[UTILS_HASHED_LENGTH];
            int32_t inode_size = inode_getsize(&inode);
            //take the min
            int32_t size_file = UTILS_HASHED_LENGTH > inode_size ? inode_size : UTILS_HASHED_LENGTH;
            int sector_bytes = 0;
            int ret = 1;
            do {
                ret = filev6_readblock(&fv6, &buffer[sector_bytes]);
                sector_bytes += SECTOR_SIZE;
            } while (ret > 0 && sector_bytes < UTILS_HASHED_LENGTH);
            if (ret < 0) {
                return ret;
            }
            utils_print_SHA_buffer(buffer, size_file);
        }
        return ERR_NONE;
    }

}

int utils_print_sha_allfiles(const struct unix_filesystem *u)
{
    M_REQUIRE_NON_NULL(u);
    pps_printf("Listing inodes SHA\n");
    for (uint16_t sector_nb = u->s.s_inode_start; sector_nb < u->s.s_isize + u->s.s_inode_start; sector_nb++) {
        uint8_t sector[SECTOR_SIZE];
        int err_sector = sector_read(u->f, sector_nb, sector);
        if (err_sector != 0) {
            return err_sector;
        }
        for (uint16_t inode_index = 0; inode_index < INODES_PER_SECTOR; inode_index++) {
            if (sector_nb == u->s.s_inode_start && inode_index == 0) inode_index++; //cannot read inode 0 of sector 0
            uint16_t curr_relative_sector = sector_nb - u->s.s_inode_start;
            int err_print = utils_print_shafile(u, inode_index + curr_relative_sector * INODES_PER_SECTOR);
            if (err_print != 0 && err_print != ERR_UNALLOCATED_INODE) {
                return err_print;
            }
        }
    }
    return ERR_NONE;
}

int utils_print_bitmaps(const struct unix_filesystem *u)
{
    M_REQUIRE_NON_NULL(u);
    bm_print("INODES", u->ibm);
    bm_print("SECTORS", u->fbm);
    return ERR_NONE;
}





