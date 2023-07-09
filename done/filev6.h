#pragma once

/**
 * @file filev6.h
 * @brief accessing the UNIX v6 filesystem -- file part of inode/file layer
 *
 * @author Edouard Bugnion
 * @date summer 2022
 */

#include "unixv6fs.h"
#include "mount.h"

#ifdef __cplusplus
extern "C" {
#endif

struct filev6 {
    struct unix_filesystem *u;    // the filesystem
    uint16_t i_number;            // the inode number (on disk)
    struct inode i_node;          // the content of the inode
    int32_t offset;               // the current cursor within the file (in bytes)
};

/* *************************************************** *
 * TODO WEEK 05										   *
 * *************************************************** */
/**
 * @brief open the file corresponding to a given inode; set offset to zero
 * @param u the filesystem (IN)
 * @param inr the inode number (IN)
 * @param fv6 the complete filev6 data structure (OUT)
 * @return 0 on success; the appropriate error code (<0) on error
 */
int filev6_open(const struct unix_filesystem *u, uint16_t inr, struct filev6 *fv6);

/* *************************************************** *
 * TODO WEEK 08										   *
 * *************************************************** */
/**
 * @brief change the current offset of the given file to the one specified
 * @param fv6 the filev6 (IN-OUT; offset will be changed)
 * @param off the new offset of the file
 * @return 0 on success; <0 on error
 */
int filev6_lseek(struct filev6 *fv6, int32_t offset);

/* *************************************************** *
 * TODO WEEK 05										   *
 * *************************************************** */
/**
 * @brief read at most SECTOR_SIZE from the file at the current cursor
 * @param fv6 the filev6 (IN-OUT; offset will be changed)
 * @param buf points to SECTOR_SIZE bytes of available memory (OUT)
 * @return >0: the number of bytes of the file read; 0: end of file;
 *             the appropriate error code (<0) on error
 */
int filev6_readblock(struct filev6 *fv6, void *buf);

/* *************************************************** *
 * TODO WEEK 1										   *
 * *************************************************** */
/**
 * @brief create a new filev6
 * @param u the filesystem (IN)
 * @param mode the mode of the file
 * @param fv6 the filev6 (OUT; i_node and i_number will be changed)
 * @return 0 on success; <0 on error
 */
int filev6_create(struct unix_filesystem *u, uint16_t mode, struct filev6 *fv6);


/* *************************************************** *
 * TODO WEEK 12										   *
 * *************************************************** */
/**
 * @brief write the len bytes of the given buffer on disk to the given filev6
 * @param fv6 the filev6 (IN)
 * @param buf the data we want to write (IN)
 * @param len the length of the bytes we want to write
 * @return 0 on success; <0 on error
 */
int filev6_writebytes(struct filev6 *fv6, const void *buf, size_t len);


#ifdef __cplusplus
}
#endif

