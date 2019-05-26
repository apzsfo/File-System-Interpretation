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
#include <math.h>

#include "ext2_fs.h"

void err_msg_and_exit_1(char* format, ...) {
    va_list argp;
    va_start(argp, format);
    vfprintf(stderr, format, argp);
    fprintf(stderr, ": %s\n", strerror(errno));
    exit(1);
}

int img_fd = -1;
unsigned int pointers_per_block = 0;
unsigned int block_size = 0;

void scan_ind_block(unsigned int inode_num, int level, unsigned int offset, unsigned int block_num, unsigned int offset_inc) {
    if (level == 0) {
        return;
    }
    unsigned int block_ptrs[pointers_per_block];
    pread(img_fd, block_ptrs, block_size, block_num * block_size);
    level--;
    offset_inc /= pointers_per_block;
    for (unsigned int i = 0; i < pointers_per_block; i++) {
        if (block_ptrs[i] == 0) {
            continue;
        }
        printf(
               "INDIRECT,%d,%d,%d,%d,%d\n",
               inode_num,
               level+1,
               offset,
               block_num,
               block_ptrs[i]
        );
        scan_ind_block(inode_num, level, offset, block_ptrs[i], offset_inc);
        offset += offset_inc;
    }
}

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "Requires exactly 1 argument\n");
        exit(1);
    }
    
    img_fd = open(argv[1], O_RDONLY);
    if (img_fd == -1) {
        err_msg_and_exit_1("Error opening file system image");
    }
    
    struct ext2_super_block super_block;
    pread(img_fd, &super_block, sizeof(struct ext2_super_block), 1024); //superblock summaries
    block_size = 1024 << super_block.s_log_block_size;
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
    
    pointers_per_block = block_size / sizeof(unsigned int);
    
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
        
        { // TODO loop
            unsigned int j = 0; // TODO inode num
            struct ext2_inode inode;
            char file_type = 'd'; //TODO
            
            if (file_type == 'd') {
                for (unsigned int k = 0; k < 12; k++) {
                    if (inode.i_block[k] == 0) {
                        continue;
                    }
                    unsigned int dirent_loc = inode.i_block[k] * block_size;
                    struct ext2_dir_entry dir_entry;
                    for (__uint32_t dirent_byte_offset = 0; dirent_byte_offset < block_size; dirent_byte_offset += dir_entry.rec_len) {
                        pread(img_fd, &dir_entry, sizeof(struct ext2_dir_entry), dirent_loc + dirent_byte_offset);
                        if (dir_entry.inode == 0) {
                            continue;
                        }
                        char dir_name[dir_entry.name_len+1];
                        for (int l = 0; l < dir_entry.name_len; l++) {
                            dir_name[l] = dir_entry.name[l];
                        }
                        dir_name[dir_entry.name_len] = '\0';
                        printf(
                               "DIRENT,%d,%d,%d,%d,%d,'%s'\n",
                               j,
                               dirent_byte_offset,
                               dir_entry.inode,
                               dir_entry.rec_len,
                               dir_entry.name_len,
                               dir_name
                        );
                    }
                }
                struct ext2_dir_entry entry;
            }
            
            if (file_type == 'f' || file_type == 'd') {
                unsigned int offset = 12;
                unsigned int offset_inc = pointers_per_block;
                if (inode.i_block[12] != 0)
                    scan_ind_block(j, 1, offset, inode.i_block[12], offset_inc);
                
                offset += offset_inc;
                offset_inc *= pointers_per_block;
                if (inode.i_block[13] != 0)
                    scan_ind_block(j, 2, offset, inode.i_block[13], offset_inc);
                
                offset += offset_inc;
                offset_inc *= pointers_per_block;
                if (inode.i_block[14] != 0)
                    scan_ind_block(j, 2, offset, inode.i_block[14], offset_inc);
            }
        }
    }
    
    return 0;
}
