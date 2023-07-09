/**
 * @file u6fs.c
 * @brief Command line interface
 *
 * @author Édouard Bugnion, Ludovic Mermod
 * @date 2022
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>

#include "error.h"
#include "mount.h"
#include "u6fs_utils.h"
#include "inode.h"
#include "direntv6.h"
#include "u6fs_fuse.h"
#define MAX_SIZE (4*1024)
#define FTELL_ERROR (-1L)
/* *************************************************** *
 * TODO WEEK 04-07: Add more messages                  *
 * *************************************************** */
static void usage(const char *execname, int err)
{
    if (err == ERR_INVALID_COMMAND) {
        pps_printf("Available commands:\n");
        pps_printf("%s <disk> sb\n%s <disk> inode\n%s <disk> cat1 <inr>\n%s <disk> shafiles\n%s <disk> tree\n"
                   "%s <disk> fuse <mountpoint>\n%s <disk> bm\n"
                   "%s <disk> mkdir </path/to/newdir>\n%s <disk> add <destination file path> <source file path>\n"
                   , execname, execname,execname,execname, execname,execname, execname, execname, execname);
    } else if (err > ERR_FIRST && err < ERR_LAST) {
        pps_printf("%s: Error: %s\n", execname, ERR_MESSAGES[err - ERR_FIRST]);
    }
    else {
        pps_printf("%s: Error: %d (UNDEFINED ERROR)\n", execname, err);
    }
}


#define CMD(a, b) (strcmp(argv[2], a) == 0 && argc == (b))

/* *************************************************** *
 * TODO WEEK 04-11: Add more commands                  *
 * *************************************************** */
/**
 * @brief Runs the command requested by the user in the command line, or returns ERR_INVALID_COMMAND if the command is not found.
 *
 * @param argc (int) the number of arguments in the command line
 * @param argv (char*[]) the arguments of the command line, as passed to main()
 */
int u6fs_do_one_cmd(int argc, char *argv[])
{

    if (argc < 3) return ERR_INVALID_COMMAND;
    struct unix_filesystem u = {0};
    int error = mountv6(argv[1], &u), err2 = 0;

    if (error != ERR_NONE) {
        debug_printf("Could not mount fs%s", "\n");
        return error;
    }
    if (CMD("inode", 3)) {
        error = inode_scan_print(&u);
    }
    else if (CMD("sb", 3)) {
        error = utils_print_superblock(&u);
    }
    else if (CMD("shafiles", 3)) {
        error = utils_print_sha_allfiles(&u);
    }
    else if(CMD("cat1", 4)){
        int atoided = atoi(argv[3]);
        if(atoided==0 || atoided==INT_MAX || atoided==INT_MIN){
            error = ERR_BAD_PARAMETER;
        }else {
            error = utils_cat_first_sector(&u, (uint16_t) atoi(argv[3]));
        }
    }
    else if(CMD("tree", 3)){
        error = direntv6_print_tree(&u, ROOT_INUMBER, "");
    }
    else if(CMD("fuse", 4)){
        error = u6fs_fuse_main(&u, argv[3]);
    }
    else if(CMD("bm", 3)){
        error = utils_print_bitmaps(&u);
    }
    else if(CMD("mkdir", 4)){
        int create = direntv6_create(&u, argv[3], IFDIR);
        if(create<0){
            error=create;
        }else{
            error =ERR_NONE;
        }
    }
    else if(CMD("add", 5)){
        /**
         * faire un fseek sur le fichier local avec la constante bien set pour qu il mette l offset a la fin du fichier
         * faire un ftell pour voir ou est l offset du fichier (recuperer la taille)
         * prendre la taille t a min(4K0, taille du fichier)
         * créer un buffer temporaire de 4K0
         * lire t elements dans buffer (fread)
         * call direntv6_addfile avec buff =tempbuff et taille = t
         */
        FILE* f = fopen(argv[4], "r");
        if(f==NULL){
            umountv6(&u);
            return ERR_IO;
        }
        int seek = fseek(f, 0, SEEK_END);
        if(seek!=0) {
            fclose(f);
            umountv6(&u);
            return ERR_IO;
        }
        long int size = ftell(f);
        if(size == FTELL_ERROR){
            fclose(f);
            umountv6(&u);
            return ERR_IO;
        }
        if(size>MAX_SIZE){
            fclose(f);
            umountv6(&u);
            return ERR_BAD_PARAMETER;
        }
        char buff[MAX_SIZE];
        int seek2 = fseek(f, 0, SEEK_SET);
        if(seek2!=0){
            fclose(f);
            umountv6(&u);
            return ERR_IO;
        }
        size_t read = fread(buff, sizeof(char),size ,f);
        fclose(f);
        if(read<size){
            umountv6(&u);
            return ERR_IO;
        }
        //donne tous les droits
        error = direntv6_addfile(&u, argv[3], (IREAD|IEXEC|IWRITE), buff, size);
    }
    else {
        error = ERR_INVALID_COMMAND;
    }
    err2 = umountv6(&u);
    return (error == ERR_NONE ? err2 : error);
}

#ifndef FUZZ

int main(int argc, char *argv[])
{
    //le main
    int ret = u6fs_do_one_cmd(argc, argv);
    if (ret != ERR_NONE) {
        usage(argv[0], ret);
    }
    return ret;

}
#endif