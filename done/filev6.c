#include "filev6.h"
#include "inode.h"
#include "error.h"
#include "sector.h"
#include "string.h"
#define MEDIUM_FILESYS_SIZE (7*256)


int filev6_writesector_residual(struct filev6 *fv6, const void *buf, size_t len, size_t size_in_sectors, size_t residual, size_t inode_size);


int filev6_open(const struct unix_filesystem *u, uint16_t inr, struct filev6 *fv6) {
    M_REQUIRE_NON_NULL(u);
    M_REQUIRE_NON_NULL(fv6);

    int error = inode_read(u, inr, &(fv6->i_node));
    if (error != ERR_NONE) {
        return error;
    }
    fv6->offset = 0;
    fv6->u = u;
    fv6->i_number = inr;
    return ERR_NONE;
}

int filev6_readblock(struct filev6 *fv6, void *buf) {
    M_REQUIRE_NON_NULL(fv6);
    M_REQUIRE_NON_NULL(buf);
    int32_t size_of_file = inode_getsize(&(fv6->i_node));
    //if everything was read already, just return 0
    if (fv6->offset == size_of_file) {
        return 0;
    }
    int sector_on_disk = inode_findsector(fv6->u, &(fv6->i_node), (fv6->offset) / SECTOR_SIZE);
    if (sector_on_disk < 0) {
        return sector_on_disk;
    }
    uint8_t sector_buf[SECTOR_SIZE];
    int read = sector_read(fv6->u->f, sector_on_disk, sector_buf);
    if (read < 0) {
       return read;
    }

    if (size_of_file - (fv6->offset) <= SECTOR_SIZE) {
       int to_ret = size_of_file - (fv6->offset);
       fv6->offset = size_of_file;
       memcpy(buf , sector_buf, to_ret);
       return to_ret;
    } else {
       fv6->offset += SECTOR_SIZE;
       memcpy(buf , sector_buf, SECTOR_SIZE);
       return SECTOR_SIZE;
    }
}

int filev6_lseek(struct filev6 *fv6, int32_t offset){
   M_REQUIRE_NON_NULL(fv6);
   int32_t size = inode_getsize(&(fv6->i_node));
   if(offset>size || offset<0){
       return ERR_OFFSET_OUT_OF_RANGE;
   }

   if(offset!=size && offset%SECTOR_SIZE!=0){
       return ERR_BAD_PARAMETER;
   }

   fv6->offset = offset;
   return ERR_NONE;
}

int filev6_create(struct unix_filesystem *u, uint16_t mode, struct filev6 *fv6){
    M_REQUIRE_NON_NULL(u);
    M_REQUIRE_NON_NULL(fv6);
    int inr = inode_alloc(u);
    if (inr < 0) {
        return inr;
    }
    //filling the fields, and not calling filev6_open to avoid an unecessary call to
    //inode_read (an assistant said it was fine)
    fv6->offset = 0;
    fv6->i_node.i_mode = mode | IALLOC;
    fv6->i_number = inr;
    fv6->u=u;
    int write = inode_write(u, inr, &fv6->i_node);
    if (write != ERR_NONE) {
        return write;
    }
    return ERR_NONE;
}

int filev6_writesector(struct filev6 *fv6, const void *buf, size_t len_to_write, size_t file_size){
    M_REQUIRE_NON_NULL(fv6);
    M_REQUIRE_NON_NULL(buf);
    int free_sector = bm_find_next(fv6->u->fbm);
    if(free_sector <0){
        return free_sector;
    }
    int8_t sector_bur[SECTOR_SIZE];
    memcpy(sector_bur, buf, len_to_write);
    int err = sector_write(fv6->u->f, free_sector, sector_bur);
    if(err <0){
        return err;
    }
    int error = inode_setsize(&fv6->i_node, file_size+len_to_write);
    if(error!=ERR_NONE){
        return error;
    }

    int index = inode_getsize(&fv6->i_node)/SECTOR_SIZE;
    fv6->i_node.i_addr[index] = free_sector;
    error = inode_write(fv6->u, fv6->i_number, &fv6->i_node);
    return error;

}


int filev6_writebytes(struct filev6 *fv6, const void *buf, size_t len){
    M_REQUIRE_NON_NULL(fv6);
    M_REQUIRE_NON_NULL(buf);
    int32_t inode_size = inode_getsize(&fv6->i_node);
    int32_t size_in_sectors = inode_size/SECTOR_SIZE+1;
    if(size_in_sectors > MEDIUM_FILESYS_SIZE){
        return ERR_FILE_TOO_LARGE;
    }
    int32_t residual = inode_size % SECTOR_SIZE;
    //odd case
    int to_write_small_sector = SECTOR_SIZE - residual > len ? len : SECTOR_SIZE - residual;
    int to_write_total = len;
    size_t written = 0;
    if (residual != 0) {
        int write_residual = filev6_writesector_residual(fv6, buf, to_write_small_sector, size_in_sectors, residual, inode_size);
        written += to_write_small_sector;
        to_write_total -= to_write_small_sector;
    }
    //while loop for other case
    while (to_write_total > 0) {
        size_t min_size = to_write_total > SECTOR_SIZE ? SECTOR_SIZE : to_write_total;
        int write_sec = filev6_writesector(fv6, &buf[written], min_size, inode_size + written);
        if (write_sec < 0) {
            return write_sec;
        }
        to_write_total -= min_size;
        written += min_size;
    }
    return ERR_NONE;
}


int filev6_writesector_residual(struct filev6 *fv6, const void *buf, size_t len_to_write, size_t size_in_sectors, size_t residual, size_t inode_size){
    int last_sector = inode_findsector(fv6->u, &fv6->i_node, size_in_sectors-1);
    if (last_sector < 0) {
        return last_sector;
    }
    int8_t buffer[SECTOR_SIZE] = {0};
    int read = sector_read(fv6->u->f, last_sector, buffer);
    if (read < 0) {
        return read;
    }

    memcpy(&buffer[residual], buf, len_to_write);
    int write = sector_write(fv6->u->f, last_sector, buffer);
    if (write < 0) {
        return write;
    }
    int set = inode_setsize(&fv6->i_node, inode_size + len_to_write);
    inode_write(fv6->u, fv6->i_number, &fv6->i_node);
    if (set < 0) {
        return set;
    }
    return ERR_NONE;
}
