#include "direntv6.h"
#include "error.h"

#include "inode.h"
#include "filev6.h"
#include "string.h"
#include "stdlib.h"
#include "filev6.h"

void free_ptr(void** ptr);

int direntv6_dirlookup_core(const struct unix_filesystem *u, uint16_t inr, const char *entry, size_t length);

int direntv6_opendir(const struct unix_filesystem *u, uint16_t inr, struct directory_reader *d)
{
    M_REQUIRE_NON_NULL(u);
    M_REQUIRE_NON_NULL(d);
    struct filev6 fv6;
    int ret = filev6_open(u, inr, &fv6);
    if (ret < 0) {
        return ret;
    }
    if (!(fv6.i_node.i_mode & IFDIR)) {
        return ERR_INVALID_DIRECTORY_INODE;
    }
    d->cur = 0;
    d->last = 0;
    d->fv6 = fv6;
    return ERR_NONE;
}

int direntv6_readdir(struct directory_reader *d, char *name, uint16_t *child_inr)
{
    M_REQUIRE_NON_NULL(d);
    M_REQUIRE_NON_NULL(name);
    M_REQUIRE_NON_NULL(child_inr);
    if (d->cur == d->last) {
        int bytes_read = filev6_readblock(&d->fv6, d->dirs);
        if (bytes_read <= 0) {
            return bytes_read;
        }
        d->last = bytes_read / sizeof(struct direntv6);
        d->cur = 0;//next to read
    }
    *child_inr = d->dirs[d->cur].d_inumber;
    strncpy(name, d->dirs[d->cur].d_name, DIRENT_MAXLEN);
    name[DIRENT_MAXLEN] = '\0';
    ++d->cur;
    return 1;
}

int direntv6_print_tree(const struct unix_filesystem *u, uint16_t inr, const char *prefix)
{
    M_REQUIRE_NON_NULL(u);
    M_REQUIRE_NON_NULL(prefix);
    struct directory_reader d_reader;
    int ret = direntv6_opendir(u, inr, &d_reader);
    if (ret < 0) {
        return ret;
    }
    char *name = calloc(DIRENT_MAXLEN + 1, 1);
    if(name==NULL) {
        return ERR_NOMEM;
    }
    uint16_t child_inr;
    int ret_readdir = direntv6_readdir(&d_reader, name, &child_inr);
    if (ret_readdir < 0) {
        return ret_readdir;
    }
    if (inr == ROOT_INUMBER) {
        pps_printf("DIR /%s\n", prefix);
    } else {
        pps_printf("DIR %s/\n", prefix);
    }
    while (ret_readdir > 0) {
        struct inode inode = {0};
        int ret_inode = inode_read(u, child_inr, &inode);
        if (ret_inode != 0) return ret_inode;
        if(inode.i_mode & IFDIR) {
            char* new_prefix = calloc(strlen(prefix) + strlen(name) + 2, sizeof(char));//+1 pour le / et +1 pour le \0
            if (new_prefix == NULL) {
                return ERR_NOMEM;
            }
            int concat = snprintf(new_prefix, strlen(prefix) + strlen(name) + 2, "%s/%s", prefix, name);
            if (concat < 0) {
                return ERR_NOMEM;
            }
            ret_readdir = direntv6_print_tree(u, child_inr, new_prefix);
            if(ret_readdir!=ERR_NONE) {
                return ret_readdir;
            }
            free_ptr((void**)&new_prefix);
        } else {
            pps_printf("FIL %s/%s\n", prefix, name);
        }
        ret_readdir = direntv6_readdir(&d_reader, name, &child_inr);
    }
    free_ptr((void**)&name);
    return ret_readdir;
}

int direntv6_dirlookup(const struct unix_filesystem *u, uint16_t inr, const char *entry)
{
    M_REQUIRE_NON_NULL(u);
    M_REQUIRE_NON_NULL(entry);
    size_t length = strlen(entry);
    return direntv6_dirlookup_core(u, inr, entry, length);
}
int direntv6_dirlookup_core(const struct unix_filesystem *u, uint16_t inr, const char *entry, size_t length)
{
    M_REQUIRE_NON_NULL(u);
    M_REQUIRE_NON_NULL(entry);

    //base cases:
    if (length == 0) return inr;
    //root case
    if (inr == 1 && length == 1 && !strncmp(entry, "/", 1)) return inr;
    size_t dir_length = 0;
    //Length of parenthesis sequence
    size_t parenthesis_length = 0;
    while(parenthesis_length < length && entry[0] == '/') {
        entry++;
        parenthesis_length++;
    }
    //extract sub-directory
    if (parenthesis_length == length) return ERR_NO_SUCH_FILE;
    while (dir_length + parenthesis_length < length && entry[dir_length] != '/') {
        dir_length++;
    }
    char *sought_dir = calloc(dir_length + 1, sizeof(char));
    if (sought_dir == NULL) return ERR_NOMEM;

    sought_dir = strncpy(sought_dir, entry, dir_length);

    if (sought_dir == NULL) return ERR_NOMEM;
    sought_dir[dir_length] = '\0';

    entry += dir_length;

    struct directory_reader d = {0};
    int open = direntv6_opendir(u, inr, &d);
    if (open != 0) {
        return open;
    }
    char *name = calloc(DIRENT_MAXLEN + 1, sizeof(char));
    if (name == NULL) {
        return ERR_NOMEM;
    }
    uint16_t inode_read = 0;
    int dir_read = direntv6_readdir(&d, name, &inode_read);
    name[DIRENT_MAXLEN] = '\0';
    while(dir_read > 0) {
        if(!strcmp(sought_dir, name)) {
            free_ptr((void**)&sought_dir);
            free_ptr((void**)&name);
            return direntv6_dirlookup_core(u, inode_read, entry, length - dir_length - parenthesis_length);
        }
        dir_read = direntv6_readdir(&d, name, &inode_read);
        name[DIRENT_MAXLEN] = '\0';
    }

    free_ptr((void**)&sought_dir);
    free_ptr((void**)&name);
    if (dir_read == 0) {
        return ERR_NO_SUCH_FILE;
    } else {
        return dir_read;
    }
}

void free_ptr(void** ptr)
{
    if (ptr == NULL) return;
    free(*ptr);
    *ptr = NULL;
}
int split_name_direntv6_create(const char *entry, char** relative_name, char** absolute_name){
    M_REQUIRE_NON_NULL(entry);
    size_t length = strlen(entry);
    size_t relative_name_length =0;
    const char* p;
    for(p = entry+length-1; relative_name_length<length && *p!='/' ;--p){
        ++relative_name_length;
    }
    if(relative_name_length>DIRENT_MAXLEN) {
        return ERR_FILENAME_TOO_LONG;
    }
    // remove the aditionals / at then end of the absoulte path
    char* endabsolute;
    size_t absolute_name_length =length-relative_name_length;
    for(endabsolute = p; absolute_name_length>0 && *endabsolute=='/' ; --endabsolute) {
        --absolute_name_length;
    }
    * relative_name = calloc(relative_name_length+1, 1);
    if(relative_name==NULL) {
        return ERR_NOMEM;
    }
    ++p;
    strcpy(*relative_name, p);
    * absolute_name = calloc(absolute_name_length+1, 1);
    if(absolute_name==NULL){
        free_ptr((void**)relative_name);
        return ERR_NOMEM;
    }
    strncpy(*absolute_name, entry, absolute_name_length);
    return ERR_NONE;
}

int create_dirent(struct unix_filesystem *u, uint16_t mode, char** relative_name, char** absolute_name, int inr_parent){
    M_REQUIRE_NON_NULL(u);
    M_REQUIRE_NON_NULL(relative_name);
    M_REQUIRE_NON_NULL(absolute_name);
    struct filev6 fv6_child = {0};
    int create_file_child = filev6_create(u, mode, &fv6_child);
    if(create_file_child<0){
        free_ptr((void**)relative_name);
        free_ptr((void**)absolute_name);
        return create_file_child;
    }
    uint16_t inode_allocated = fv6_child.i_number;
    struct direntv6 mydirentv6 = {0};
    mydirentv6.d_inumber=inode_allocated;
    strcpy(mydirentv6.d_name, *relative_name);
    struct filev6 fv6_parent = {0};
    int fv6_open = filev6_open(u, inr_parent, &fv6_parent);
    if(fv6_open<0) {
        free_ptr((void**)relative_name);
        free_ptr((void**)absolute_name);
        return fv6_open;
    }
    int writebyte = filev6_writebytes(&fv6_parent, &mydirentv6, sizeof(struct direntv6)); //metre buf a direntv6 et len = ongueur de direntv6
    if(writebyte<0) {
        free_ptr((void**)relative_name);
        free_ptr((void**)absolute_name);
        return writebyte;
    }
    free_ptr((void**)relative_name);
    free_ptr((void**)absolute_name);
    return inode_allocated;
}

int direntv6_create(struct unix_filesystem *u, const char *entry, uint16_t mode){
    M_REQUIRE_NON_NULL(u);
    M_REQUIRE_NON_NULL(entry);
    char* relative_name;
    char* absolute_name;
    int split = split_name_direntv6_create(entry, &relative_name, &absolute_name);
    if( split!=ERR_NONE) {
        return split;
    }
    int inr_parent = direntv6_dirlookup(u, ROOT_INUMBER, absolute_name);
    if(inr_parent<0) {
        free_ptr((void**)&relative_name);
        free_ptr((void**)&absolute_name);
        return inr_parent;
    }
    int dir_lookup_child = direntv6_dirlookup(u, inr_parent, relative_name);
    if(dir_lookup_child!=ERR_NO_SUCH_FILE) {
        if(dir_lookup_child>=0) {
            free_ptr((void**)&relative_name);
            free_ptr((void**)&absolute_name);
            return ERR_FILENAME_ALREADY_EXISTS;
        } else {
            free_ptr((void**)&relative_name);
            free_ptr((void**)&absolute_name);
            return dir_lookup_child;
        }
    }
    return create_dirent(u, mode, &relative_name, &absolute_name, inr_parent);
}
int direntv6_addfile(struct unix_filesystem *u, const char *entry, uint16_t mode, char *buf, size_t size)
{
    M_REQUIRE_NON_NULL(u);
    M_REQUIRE_NON_NULL(entry);
    M_REQUIRE_NON_NULL(buf);
    int inr = direntv6_create(u, entry, mode);
    if (inr < 0) return inr;
    struct filev6 fv6;
    struct inode inode;
    int read = inode_read(u, inr, &inode);
    if (read < 0) {
        return read;
    }
    fv6.i_number = inr;
    int open = filev6_open(u, inr, &fv6);
    if (open <0) {
        return open;
    }
    int write = filev6_writebytes(&fv6, buf, size);
    if (write < 0) {
        return write;
    }
    return ERR_NONE;
}


