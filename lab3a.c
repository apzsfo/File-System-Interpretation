//NAME: Ryan Nemiroff, Andrew Zeff
//EMAIL: ryguyn@gmail.com, apzsfo@g.ucla.edu
//ID: 304903942, 804994987

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

void err_msg_and_exit_1(char* format, ...) { //error warning function
    va_list argp;
    va_start(argp, format);
    vfprintf(stderr, format, argp);
    fprintf(stderr, ": %s\n", strerror(errno));
    exit(1);
}

int img_fd = -1;
unsigned int pointers_per_block = 0;
unsigned int block_size = 0;

void scan_ind_block(unsigned int inode_num, int level, unsigned int offset, unsigned int block_num, unsigned int offset_inc) { //scan independent blocks
    if (level == 0) {
        return;
    }
    unsigned int block_ptrs[pointers_per_block];
    int scan = pread(img_fd, block_ptrs, block_size, block_num * block_size);
    if(scan < 0)
    {
        fprintf(stderr, "pread error\n");
        exit(2);
    }
    level--;
    offset_inc /= pointers_per_block;
    for (unsigned int i = 0; i < pointers_per_block; i++) {
        if (block_ptrs[i] != 0) {
            printf(
                   "INDIRECT,%d,%d,%d,%d,%d\n",
                   inode_num,
                   level+1,
                   offset,
                   block_num,
                   block_ptrs[i]
            );
            scan_ind_block(inode_num, level, offset, block_ptrs[i], offset_inc);
        }
        offset += offset_inc;
    }
}

int main(int argc, char** argv) {
    if (argc != 2) { //check for correct number of arguments
        fprintf(stderr, "Requires exactly 1 argument\n");
        exit(1);
    }
    
    img_fd = open(argv[1], O_RDONLY); //check for correct image file opening
    if (img_fd == -1) {
        err_msg_and_exit_1("Error opening file system image");
    }
    
    struct ext2_super_block super_block;
    int first_read = pread(img_fd, &super_block, sizeof(struct ext2_super_block), 1024); //superblock summaries
    if(first_read < 0)
    {
        fprintf(stderr, "pread error\n");
        exit(2);
    }
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
    
    int inodes_per_group = super_block.s_inodes_per_group; //group summaries
    int inode_size = super_block.s_inode_size;
    int inode_buffer_size = inodes_per_group * inode_size;
    pointers_per_block = block_size / sizeof(unsigned int);
    
    int groups_with_full_blocks = super_block.s_blocks_count / super_block.s_blocks_per_group; //all the inputs of the CSV
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
        exit(2);
    }
    
    unsigned int num_blocks = super_block.s_blocks_per_group;
    unsigned int num_inodes = super_block.s_inodes_per_group;
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
        int f_read = pread(img_fd, block_bitmap, block_bitmap_bytes, groups[i].bg_block_bitmap * block_size);
        if(f_read < 0)
        {
            fprintf(stderr, "pread error\n");
            exit(2);
        }
        for (unsigned int j = 0; j < num_blocks; j++) {
            if ((block_bitmap[j/8] & (1<<(j%8))) == 0) {
                printf("BFREE,%d\n", i*super_block.s_blocks_per_group + j + 1);
            }
        }
        
        unsigned int inode_bitmap_bytes = (num_inodes-1)/8 + 1;
        unsigned char inode_bitmap[inode_bitmap_bytes];
        int ft_read = pread(img_fd, inode_bitmap, inode_bitmap_bytes, groups[i].bg_inode_bitmap * block_size);
        if(ft_read < 0)
        {
            fprintf(stderr, "pread error\n");
            exit(2);
        }
        for (unsigned int j = 0; j < num_inodes; j++) {
            if ((inode_bitmap[j/8] & (1<<(j%8))) == 0) {
                printf("IFREE,%d\n", i*super_block.s_inodes_per_group + j + 1);
            }
        }
        
        //i-node summary
        struct ext2_inode inodes[inodes_per_group];
        ssize_t i_read = pread(img_fd, inodes, inode_buffer_size, groups[i].bg_inode_table*block_size);
        if(i_read < 0)
        {
            fprintf(stderr, "inode read error\n");
            exit(2);
        }
        unsigned int j = 0;
        for(; j < num_inodes; j++)
        {
            int inode_number = j+1;
            char file_type = '?';
            if(inodes[j].i_mode && inodes[j].i_links_count)
            {
                if((inodes[j].i_mode & 0x8000) == 0)
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
                strftime(last_change, 50, "%m/%d/%y %H:%M:%S", gmtime(&l));
                strftime(mod, 50, "%m/%d/%y %H:%M:%S", gmtime(&m));
                strftime(acc, 50, "%m/%d/%y %H:%M:%S", gmtime(&a));
                
                dprintf(1, "INODE,%d,%c,%o,%d,%d,%d,%s,%s,%s,%d,%d\n", inode_number, file_type, inodes[j].i_mode & 0x0FFF, inodes[j].i_uid, inodes[j].i_gid, inodes[j].i_links_count, last_change, mod, acc, inodes[j].i_size, inodes[j].i_blocks);
                dprintf(1, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n", inodes[j].i_block[0],inodes[j].i_block[1],inodes[j].i_block[2], inodes[j].i_block[3], inodes[j].i_block[4], inodes[j].i_block[5], inodes[j].i_block[6], inodes[j].i_block[7], inodes[j].i_block[8], inodes[j].i_block[9], inodes[j].i_block[10], inodes[j].i_block[11], inodes[j].i_block[12], inodes[j].i_block[13], inodes[j].i_block[14]);
                if (file_type == 'd') { //directory entries
                    for (unsigned int k = 0; k < 12; k++) {
                        if (inodes[j].i_block[k] == 0) {
                            continue;
                        }
                        unsigned int dirent_loc = inodes[j].i_block[k] * block_size;
                        struct ext2_dir_entry dir_entry;
                        for (__uint32_t dirent_byte_offset = 0; dirent_byte_offset < block_size; dirent_byte_offset += dir_entry.rec_len) {
                            int diread = pread(img_fd, &dir_entry, sizeof(struct ext2_dir_entry), dirent_loc + dirent_byte_offset);
                            if(diread < 0)
                            {
                                fprintf(stderr, "pread error\n");
                                exit(2);
                            }
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
                                   inode_number,
                                   dirent_byte_offset,
                                   dir_entry.inode,
                                   dir_entry.rec_len,
                                   dir_entry.name_len,
                                   dir_name
                            );
                        }
                    }
                }
                //indirect block entries
                if (file_type == 'f' || file_type == 'd') {
                    unsigned int offset = 12;
                    unsigned int offset_inc = pointers_per_block;
                    if (inodes[j].i_block[12] != 0)
                        scan_ind_block(inode_number, 1, offset, inodes[j].i_block[12], offset_inc);
                    
                    offset += offset_inc;
                    offset_inc *= pointers_per_block;
                    if (inodes[j].i_block[13] != 0)
                        scan_ind_block(inode_number, 2, offset, inodes[j].i_block[13], offset_inc);
                    
                    offset += offset_inc;
                    offset_inc *= pointers_per_block;
                    if (inodes[j].i_block[14] != 0)
                        scan_ind_block(inode_number, 3, offset, inodes[j].i_block[14], offset_inc);
                }
            }
        }
    }
    
    return 0;
}
