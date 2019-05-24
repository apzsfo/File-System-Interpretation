#include <stdio.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "ext2_fs.h"

void err_msg_and_exit_1(char* format, ...) {
    va_list argp;
    va_start(argp, format);
    vfprintf(stderr, format, argp);
    fprintf(stderr, ": %s\r\n", strerror(errno));
    exit(1);
}

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "Requires exactly 1 argument\n");
        exit(1);
    }
    
    int img_fd = open(argv[1], O_RDONLY);
    if (img_fd == -1) {
        err_msg_and_exit_1("Error opening file system image");
    }
    
    struct ext2_super_block super_block;
    pread(img_fd, &super_block, sizeof(struct ext2_super_block), 1024);
    printf(
           "SUPERBLOCK,%d,%d,%d,%d,%d,%d,%d\n",
           super_block.s_blocks_count,
           super_block.s_inodes_count,
           1024 << super_block.s_log_block_size,
           super_block.s_inode_size,
           super_block.s_blocks_per_group,
           super_block.s_inodes_per_group,
           super_block.s_first_ino
    );
    
    return 0;
}
