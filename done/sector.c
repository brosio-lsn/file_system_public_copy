#include"sector.h"
#include"unixv6fs.h"
#include"error.h"
#define NB_EL 1

int sector_read(FILE *f, uint32_t sector, void *data)
{
    M_REQUIRE_NON_NULL(f);
    M_REQUIRE_NON_NULL(data);
    long int offset = sector*SECTOR_SIZE;
    int seek = fseek(f, offset, SEEK_SET);
    size_t read = fread(data, 1, SECTOR_SIZE, f);
    if(seek==0 && read == SECTOR_SIZE) {
        return ERR_NONE;
    } else {
        return ERR_IO;
    }
}

int sector_write(FILE *f, uint32_t sector, const void *data)
{
    M_REQUIRE_NON_NULL(f);
    M_REQUIRE_NON_NULL(data);
    long int offset = sector*SECTOR_SIZE;
    int seek = fseek(f, offset, SEEK_SET);
    if (seek != 0) {
        return ERR_IO;
    }
    int write = fwrite(data, SECTOR_SIZE, NB_EL, f);

    return ERR_NONE;
}
