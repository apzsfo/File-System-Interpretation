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
#include <time.h>
#include "ext2_fs.h"

int group_count = (super.s_blocks_count-1) / super.s_blocks_per_group + 1;


void err_msg_and_exit_1(char* format, ...) {
    va_list argp;
    va_start(argp, format);
    vfprintf(stderr, format, argp);
    fprintf(stderr, ": %s\n", strerror(errno));
    exit(1);
}

void group_info()
{
    
    struct ext2_super_block groups[group_count];
    ssize_t g_read = pread(img_fd, &groups, sizeof(struct ext2_group_desc)*group_count, 1024 + (1024 << super_block.s_log_block_size));
    if(g_read == 0)
    {
        fprintf(stderr, "pread error\n");
    }
    int i = 0;
    for(; i < group_count; i++)
    {
        int blks_per_group = super_block.s_blocks_per_group;
        int i-nodes_per_group = super_block.s_inodes_per_group;
        
        if(i == group_count - 1);
        {
            blks_per_group = super_block.s_blocks_count % super_block.s_blocks_per_group;
            i-nodes_per_group = super_block.s_inodes_count % super_block.s_inodes_per_group;
        }
        dprintf(1, "GROUP,%d,%d,%d,%d,%d,%d,%d,%d\n", i, blks_per_group, i-nodes_per_group, groups[i].bg_free_blocks_count, groups[i].bg_free_inodes_count,groupBuffer[i].bg_block_bitmap,groupBuffer[i].bg_inode_bitmap,groupBuffer[i].bg_inode_table);
    }
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
    pread(img_fd, &super_block, sizeof(struct ext2_super_block), 1024); //superblock summaries
    unsigned int block_size = 1024 << super_block.s_log_block_size;
    printf(
           "SUPERBLOCK,%d,%d,%d,%d,%d,%d,%d\n",
           super_block.s_blocks_count,
           super_block.s_inodes_count,
           block_size,
           super_block.s_inode_size,
           super_block.s_blocks_per_group,
           super_block.s_inodes_per_group,
           super_block.s_first_ino
    );
    group_info(); //group summaries
    
    unsigned int i = 0; // TODO group num
    
    struct ext2_group_desc group_desc;
    unsigned int num_blocks = group_desc.s_blocks_per_group; // TODO different for last group
    unsigned int num_inodes = group_desc.s_inodes_per_group; // TODO different for last group
    
    unsigned int block_bitmap_bytes = (num_blocks-1)/8 + 1;
    unsigned char block_bitmap[block_bitmap_bytes];
    pread(img_fd, block_bitmap, block_bitmap_bytes, group_desc.bg_block_bitmap * block_size);
    for (unsigned int j = 0; j < num_blocks; j++) {
        if (block_bitmap[j/8] & (1<<(j%8)) == 0) {
            printf("BFREE,%d\n", i*super_block.s_blocks_per_group + j);
        }
    }
    
    unsigned int inode_bitmap_bytes = (num_inodes-1)/8 + 1;
    unsigned char inode_bitmap[inode_bitmap_bytes];
    pread(img_fd, inode_bitmap, inode_bitmap_bytes, group_desc.bg_inode_bitmap * block_size)
    for (unsigned int j = 0; j < num_inodes; j++) {
        if (block_bitmap[j/8] & (1<<(j%8)) == 0) {
            printf("BFREE,%d\n", i*super_block.s_inodes_per_group + j);
        }
    }
    
    return 0;
}
