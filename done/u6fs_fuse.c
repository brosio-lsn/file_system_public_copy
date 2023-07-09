/**
 * @file u6fs_fuse.c
 * @brief interface to FUSE (Filesystem in Userspace)
 *
 * @date 2022
 * @author Édouard Bugnion, Ludovic Mermod
 *  Inspired from hello.c from:
 *    FUSE: Filesystem in Userspace
 *    Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
 *
 *  This program can be distributed under the terms of the GNU GPL.
 */

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <string.h>
#include <fcntl.h>

#include <stdlib.h> // for exit()
#include "mount.h"
#include "error.h"
#include "inode.h"
#include "direntv6.h"
#include "u6fs_utils.h"
#include "u6fs_fuse.h"
#include "util.h"

static struct unix_filesystem* theFS = NULL; // usefull for tests

int fs_getattr(const char *path, struct stat *stbuf)
{
    M_REQUIRE_NON_NULL(theFS);
    M_REQUIRE_NON_NULL(path);
    M_REQUIRE_NON_NULL(stbuf);
    int inr = direntv6_dirlookup(theFS, ROOT_INUMBER, path);
    if (inr < 0) {
        return inr;
    }
    memset(stbuf, 0, sizeof(struct stat));
    struct inode inode;
    int err_inode = inode_read(theFS, inr, &inode);
    if (err_inode != 0) {
        return err_inode;
    }
    stbuf->st_ino = inr;
    stbuf->st_nlink = inode.i_nlink;
    stbuf->st_uid = inode.i_uid;
    stbuf->st_gid = inode.i_gid;
    stbuf->st_size = inode_getsize(&inode);
    //blocks
    stbuf->st_blksize = SECTOR_SIZE;

    stbuf->st_blocks = (stbuf->st_size-1)/SECTOR_SIZE + 1;
    //mode, as instructed on the website
    int file_mode = 0;
    if(inode.i_mode & IFDIR) {
        file_mode = S_IFDIR;
    } else {
        file_mode = S_IFREG;
    }
    stbuf->st_mode = S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH | file_mode;
    return ERR_NONE;
}

// Insert directory entries into the directory structure, which is also passed to it as buf
int fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset _unused, struct fuse_file_info *fi)
{
    M_REQUIRE_NON_NULL(theFS);
    M_REQUIRE_NON_NULL(path);
    M_REQUIRE_NON_NULL(buf);
    M_REQUIRE_NON_NULL(filler);
    M_REQUIRE_NON_NULL(fi);
    struct directory_reader d;
    int inr = direntv6_dirlookup(theFS, ROOT_INUMBER, path);
    if (inr < 0) {
        return inr;
    }
    int open = direntv6_opendir(theFS, inr, &d);
    if (open != 0) {
        return open;
    }
    char* name = calloc(DIRENT_MAXLEN+1, sizeof(char));
    if (name == NULL) {
        return ERR_NOMEM;
    }
    uint16_t child_inr = 0;
    int read = direntv6_readdir(&d, name, &child_inr);
    name[DIRENT_MAXLEN] = '\0';
    while(read == 1) {
        int fill = filler(buf, name, NULL, 0);
        if (fill != 0) {
            free(name);
            return ERR_NOMEM;
        }
        read = direntv6_readdir(&d, name, &child_inr);
    }
    if (read < 0) {
        free(name);
        return read;
    }
    free(name);
    int ret = filler(buf, ".", NULL, 0);
    if (ret != 0) {
        return ERR_NOMEM;
    }
    ret = filler(buf, "..", NULL, 0);
    if (ret != 0) return ERR_NOMEM;

    return ERR_NONE;
}

int fs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    M_REQUIRE_NON_NULL(path);
    M_REQUIRE_NON_NULL(buf);
    M_REQUIRE_NON_NULL(fi);
    M_REQUIRE_NON_NULL(theFS);
    int inr = direntv6_dirlookup(theFS, ROOT_INUMBER, path);
    if(inr<0) {
        return inr;
    }
    struct filev6 f;
    int fileopen = filev6_open(theFS, (uint16_t)inr, &f);
    if(fileopen<0) {
        return fileopen;
    }
    int casted_offset = (int)offset;
    int err = filev6_lseek(&f, casted_offset);
    if(err<0) {
        return err;
    }
    uint8_t sector[SECTOR_SIZE];
    int readblock;
    size_t size_copied =0; // the total size we will be able to copy
    size_t cur_offset_in_sector=offset%SECTOR_SIZE; //offset in the sector we load (only different from 0 for the first sector, because then we load sector by sector)
    size_t cur_offset_in_buffer=0;//offset to write in buffer
    while ((readblock= filev6_readblock(&f, sector)) > 0 && size_copied<size) {//keep copying while not end of file and there is space left in the buffer
        //copied size at iteration of this loop  = min(readblock, size-size_copied)
        size_t copied_size = readblock;
        if(size-size_copied<copied_size) {
            copied_size = size-size_copied;
        }
        memcpy(buf +cur_offset_in_buffer, sector+cur_offset_in_sector, copied_size);
        size_copied+=copied_size;
        cur_offset_in_sector =0; //cur_offset_in_sector can only be different from zero for the first sector
        cur_offset_in_buffer+=copied_size;
    }
    if(readblock<0) {
        return readblock;
    }
    return (int)size_copied;

}


static struct fuse_operations available_ops = {
    .getattr = fs_getattr,
    .readdir = fs_readdir,
    .read    = fs_read,
};

int u6fs_fuse_main(struct unix_filesystem *u, const char *mountpoint)
{
    M_REQUIRE_NON_NULL(mountpoint);

    theFS = u;  // /!\ GLOBAL ASSIGNMENT
    const char *argv[] = {
        "u6fs",
        "-s",               // * `-s` : single threaded operation
        "-f",              // foreground operation (no fork).  alternative "-d" for more debug messages
        "-odirect_io",      //  no caching in the kernel.
#ifdef DEBUG
        "-d",
#endif
        //  "-ononempty",    // unused
        mountpoint
    };
    // very ugly trick when a cast is required to avoid a warning
    void *argv_alias = argv;

    utils_print_superblock(theFS);
    int ret = fuse_main(sizeof(argv) / sizeof(char *), argv_alias, &available_ops, NULL);
    theFS = NULL; // /!\ GLOBAL ASSIGNMENT
    return ret;
}

#ifdef CS212_TEST
void fuse_set_fs(struct unix_filesystem *u)
{
    theFS = u;
}
#endif
