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
    int leftover_blocks = super_block.s_blocks_count % super_block.s_blocks_per_group;
    int leftover_inodes = super_block.s_inodes_count % super_block.s_inodes_per_group;
    int groups_needed_for_blocks = groups_with_full_blocks + !!(leftover_blocks);
    int groups_needed_for_inodes = groups_with_full_inodes + !!(leftover_inodes);
    int group_count = (groups_needed_for_blocks > groups_needed_for_inodes ? groups_needed_for_blocks : groups_needed_for_inodes);
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
            num_blocks = leftover_blocks;
        } else if (i > groups_with_full_blocks) {
            num_blocks = 0;
        }
        
        if (i == groups_with_full_inodes) {
            num_inodes = leftover_inodes;
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
                printf("IFREE,%d\n", i*super_block.s_inodes_per_group + j);
            }
        }
    }
    //i-node summary
    int inodes_per_group = superBuffer.s_inodes_per_group;
    int inode_size = superBuffer.s_inode_size;
    int inode_buffer_size = inodes_per_group * inode_size;
    struct ext2_inode inodes[inodes_per_group];
    int i = 0;
    for(; i < group_count; i++)
    {
        ssize_t i_read = pread(img_fd, inodes, inode_buffer_size, groups[i].bg_inode_table*block_size);
        if(i_read < 0)
        {
            fprintf(stderr, "inode read error\n");
            exit(1);
        }
        int j = 0;
        for(; j < inode_size; i++)
        {
            int inode_number = j+1;
            char file_type = '?';
            if(inodes[j].i_mode && inodes[j].i_links_count)
            {
                if((inodes[j].i_mode & 0x8000) != 0)
                {
                    if(inodes[j].i_mode & 0x4000)
                        file_type = 'd';
                }
                else
                {
                    if(inodes[j].i_mode & 0x2000)
                        file_type = 's';
                    else
                        file_type = 'f';
                }
                char last_change[50];
                char mod[50];
                char acc[50];
                time_t l = inodes[j].i_ctime;
                time_t m = inodes[j].i_mtime;
                time_t a = inodes[j].i_atime;
                strftime(last_change, 50, "%d %t", gmtime(&l));
                strftime(mod, 50, "%d %t", gmtime(&m));
                strftime(acc, 50, "%d %t", gmtime(&a));
                
                dprintf(1, "INODE,%d,%c,%o,%d,%d,%d,%s,%s,%s,%d,%d", inode_number, file_type, inodes[j].i_mode & 0x0FFF, inodes[j].i_uid, inodes[j].i_gid, inodes[j].i_links_count, last_change, mod, acc, inodes[j].i_size, inodes[j].i_blocks);
            }
        }
    }
    
    return 0;
}
