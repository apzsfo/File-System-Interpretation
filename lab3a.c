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

void err_msg_and_exit_1(char* format, ...) {
    va_list argp;
    va_start(argp, format);
    vfprintf(stderr, format, argp);
    fprintf(stderr, ": %s\n", strerror(errno));
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
    
    int groups_with_full_blocks = super_block.s_blocks_count / super_block.s_blocks_per_group;
    int groups_with_full_inodes = super_block.s_inodes_count / super_block.s_inodes_per_group;
    int group_count = (groups_with_full_blocks > groups_with_full_inodes ? groups_with_full_blocks : groups_with_full_inodes) + 1;
    struct ext2_group_desc groups[group_count];
    ssize_t g_read = pread(img_fd, groups, sizeof(struct ext2_group_desc)*group_count, 1024 + block_size);
    if(g_read == 0)
    {
        fprintf(stderr, "pread error\n");
    }
    
    int num_blocks = super_block.s_blocks_per_group;
    int num_inodes = super_block.s_inodes_per_group;
    int i = 0;
    for(; i < group_count; i++)
    {
        if (i == groups_with_full_blocks) {
            num_blocks = super_block.s_blocks_count % super_block.s_blocks_per_group;
        } else if (i > groups_with_full_blocks) {
            num_blocks = 0;
        }
        
        if (i == groups_with_full_inodes) {
            num_inodes = super_block.s_inodes_count % super_block.s_inodes_per_group;
        } else if (i > groups_with_full_inodes) {
            num_inodes = 0;
        }
        
        dprintf(1, "GROUP,%d,%d,%d,%d,%d,%d,%d,%d\n", i, num_blocks, num_inodes, groups[i].bg_free_blocks_count, groups[i].bg_free_inodes_count,groups[i].bg_block_bitmap,groups[i].bg_inode_bitmap,groups[i].bg_inode_table);
        
        // free block/inode summaries:
        unsigned int block_bitmap_bytes = (num_blocks-1)/8 + 1;
        unsigned char block_bitmap[block_bitmap_bytes];
        pread(img_fd, block_bitmap, block_bitmap_bytes, groups[i].bg_block_bitmap * block_size);
        for (unsigned int j = 0; j < num_blocks; j++) {
            if ((block_bitmap[j/8] & (1<<(j%8))) == 0) {
                printf("BFREE,%d\n", i*super_block.s_blocks_per_group + j);
            }
        }
        
        unsigned int inode_bitmap_bytes = (num_inodes-1)/8 + 1;
        unsigned char inode_bitmap[inode_bitmap_bytes];
        pread(img_fd, inode_bitmap, inode_bitmap_bytes, groups[i].bg_inode_bitmap * block_size);
        for (unsigned int j = 0; j < num_inodes; j++) {
            if ((block_bitmap[j/8] & (1<<(j%8))) == 0) {
                printf("BFREE,%d\n", i*super_block.s_inodes_per_group + j);
            }
        }
    }
    
    return 0;
}
