#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include "fs-sim.h"

#define BLOCK_SIZE 1024
#define NUM_BLOCKS 128
#define NUM_INODES 126

// Global variables
static Superblock superblock;
static char buffer[BLOCK_SIZE];
static char *current_disk;
static int current_dir_inode = 0;  // Root directory inode index

// Helper functions
static void write_block(FILE *disk, int block_num, const void *data) {
    fseek(disk, block_num * BLOCK_SIZE, SEEK_SET);
    fwrite(data, BLOCK_SIZE, 1, disk);
}

static void read_block(FILE *disk, int block_num, void *data) {
    fseek(disk, block_num * BLOCK_SIZE, SEEK_SET);
    fread(data, BLOCK_SIZE, 1, disk);
}

static int find_free_inode(void) {
    for (int i = 0; i < NUM_INODES; i++) {
        if (!(superblock.inode[i].used_size & 0x80)) {  // Check if inode is free
            return i;
        }
    }
    return -1;
}

static int get_file_inode(const char name[5], int parent_inode) {
    for (int i = 0; i < NUM_INODES; i++) {
        if ((superblock.inode[i].used_size & 0x80) &&  // Inode is in use
            (superblock.inode[i].dir_parent & 0x7F) == parent_inode &&  // Same parent
            memcmp(superblock.inode[i].name, name, 5) == 0) {  // Same name
            return i;
        }
    }
    return -1;
}

static int find_contiguous_blocks(int size) {
    if (size <= 0) return 0;

    int current_start = 1;  
    int current_count = 0;

    for (int i = 1; i < NUM_BLOCKS; i++) {
        int byte_idx = i / 8;
        int bit_idx = i % 8;
        
        if (!(superblock.free_block_list[byte_idx] & (1 << bit_idx))) {  // Block is free
            if (current_count == 0) {
                current_start = i;
            }
            current_count++;
            if (current_count == size) {
                return current_start;
            }
        } else {
            current_count = 0;
        }
    }
    return -1;
}

static void mark_blocks(int start_block, int num_blocks, int mark) {
    for (int i = 0; i < num_blocks; i++) {
        int block = start_block + i;
        if (block < 0 || block >= NUM_BLOCKS) continue;
        
        int byte_idx = block / 8;
        int bit_idx = block % 8;
        
        // Initialize superblock as used
        if (block == 0) {
            superblock.free_block_list[0] |= 1;  // Always mark first bit as used
            continue;
        }
        
        if (mark) {
            superblock.free_block_list[byte_idx] |= (1 << bit_idx);
        } else {
            superblock.free_block_list[byte_idx] &= ~(1 << bit_idx);
        }
    }
}

static int check_consistency(void) {
    // Check 1: Verify free inodes
    for (int i = 0; i < NUM_INODES; i++) {
        if (!(superblock.inode[i].used_size & 0x80)) {  // If inode is free
            if (superblock.inode[i].used_size != 0 || superblock.inode[i].start_block != 0 ||
                superblock.inode[i].dir_parent != 0) {
                return 1;
            }
            // Check if name is non-zero
            for (int j = 0; j < 5; j++) {
                if (superblock.inode[i].name[j] != 0) {
                    return 1;
                }
            }
        }
    }

    // Check 2: Valid start block and size for files
    for (int i = 0; i < NUM_INODES; i++) {
        if ((superblock.inode[i].used_size & 0x80) &&  // Inode is in use
            !(superblock.inode[i].dir_parent & 0x80)) {  // Not a directory
            int size = superblock.inode[i].used_size & 0x7F;
            if (superblock.inode[i].start_block < 1 || superblock.inode[i].start_block > 127 ||
                superblock.inode[i].start_block + size - 1 > 127) {
                return 2;
            }
        }
    }

    // Check 3: Directory attributes
    for (int i = 0; i < NUM_INODES; i++) {
        if ((superblock.inode[i].used_size & 0x80) &&  // Inode is in use
            (superblock.inode[i].dir_parent & 0x80)) {  // Is a directory
            if (superblock.inode[i].start_block != 0 ||
                (superblock.inode[i].used_size & 0x7F) != 0) {
                return 3;
            }
        }
    }

    // Check 4: Parent directory validity
    for (int i = 0; i < NUM_INODES; i++) {
        if (superblock.inode[i].used_size & 0x80) {  // Inode is in use
            int parent = superblock.inode[i].dir_parent & 0x7F;
            if (parent == 126) {
                return 4;
            }
            if (parent < 0 || parent >= NUM_INODES) {  // Invalid parent index
                return 4;
            }
            if (parent != 127 && (!(superblock.inode[parent].used_size & 0x80) ||  // Parent must be in use
                !(superblock.inode[parent].dir_parent & 0x80))) {  // Parent must be directory
                return 4;
            }
        }
    }

    // Check 5: Unique names within directories
    for (int i = 0; i < NUM_INODES; i++) {
        if ((superblock.inode[i].used_size & 0x80)) {  // Inode is in use
            int parent = superblock.inode[i].dir_parent & 0x7F;
            for (int j = i + 1; j < NUM_INODES; j++) {
                if ((superblock.inode[j].used_size & 0x80) &&  // Other inode is in use
                    (superblock.inode[j].dir_parent & 0x7F) == parent &&  // Same parent
                    memcmp(superblock.inode[i].name, superblock.inode[j].name, 5) == 0) {  // Same name
                    return 5;
                }
            }
        }
    }

    // // Check 6: Block allocation consistency
    // int block_used[NUM_BLOCKS] = {0};
    // block_used[0] = 1;  // Superblock is always used

    // // Scan all inodes and mark blocks they use
    // for (int i = 0; i < NUM_INODES; i++) {
    //     if ((superblock.inode[i].used_size & 0x80) &&  // Inode is in use
    //         !(superblock.inode[i].dir_parent & 0x80)) {  // Not a directory
    //         int size = superblock.inode[i].used_size & 0x7F;
    //         int start = superblock.inode[i].start_block;
            
    //         // Validate block range
    //         if (start + size > NUM_BLOCKS) {
    //             return 6;
    //         }
            
    //         // Check if blocks are already marked as used
    //         for (int j = 0; j < size; j++) {
    //             if (block_used[start + j]) {
    //                 return 6;  // Block allocated to multiple files
    //             }
    //             block_used[start + j] = 1;
    //         }
    //     }
    // }

    // // Compare with free space list
    // for (int i = 0; i < NUM_BLOCKS; i++) {
    //     int byte_idx = i / 8;
    //     int bit_idx = i % 8;
    //     int is_marked = (superblock.free_block_list[byte_idx] & (1 << bit_idx)) ? 1 : 0;
        
    //     if (is_marked != block_used[i]) {
    //         return 6;  // Mismatch between free space list and actual block usage
    //     }
    // }

    return 0;
}

void fs_mount(char *new_disk_name) {
    FILE *disk = fopen(new_disk_name, "r+b");
    if (!disk) {
        fprintf(stderr, "Error: Cannot find disk %s\n", new_disk_name);
        return;
    }

    // Read superblock
    read_block(disk, 0, &superblock);
    superblock.free_block_list[0] |= 1;
    fclose(disk);

    // Check consistency
    int consistency = check_consistency();
    if (consistency != 0) {
        fprintf(stderr, "Error: File system in %s is inconsistent (error code: %d)\n",
                new_disk_name, consistency);
        return;
    }

    // Update current disk and directory
    if (current_disk) free(current_disk);
    current_disk = strdup(new_disk_name);
    current_dir_inode = 0;

    // Zero out buffer
    memset(buffer, 0, 1024);
}

void fs_create(char name[5], int size) {
    if (!current_disk) {
        fprintf(stderr, "Error: No file system is mounted\n");
        return;
    }

    // Check if name exists in current directory
    if (get_file_inode(name, current_dir_inode) != -1) {
        fprintf(stderr, "Error: File or directory %s already exists\n", name);
        return;
    }

    // Find free inode
    int inode_idx = find_free_inode();
    if (inode_idx == -1) {
        fprintf(stderr, "Error: Superblock in disk %s is full, cannot create %s\n",
                current_disk, name);
        return;
    }

    // For files, find contiguous blocks
    int start_block = 0;
    if (size > 0) {
        start_block = find_contiguous_blocks(size);
        if (start_block == -1) {
            fprintf(stderr, "Error: Cannot allocate %d blocks on %s\n", size, current_disk);
            return;
        }
    }

    // Initialize inode with proper values
    memcpy(superblock.inode[inode_idx].name, name, 5);
    superblock.inode[inode_idx].used_size = 0x80 | (size & 0x7F); 
    superblock.inode[inode_idx].start_block = start_block;
    superblock.inode[inode_idx].dir_parent = (size == 0 ? 0x80 : 0) | (current_dir_inode & 0x7F);

    // Mark blocks as used
    if (size > 0) {
        mark_blocks(start_block, size, 1);
    }

    // Write superblock back to disk
    FILE *disk = fopen(current_disk, "r+b");
    write_block(disk, 0, &superblock);
    fclose(disk);
}

void fs_delete(char name[5]) {
    if (!current_disk) {
        fprintf(stderr, "Error: No file system is mounted\n");
        return;
    }

    int inode_idx = get_file_inode(name, current_dir_inode);
    if (inode_idx == -1) {
        fprintf(stderr, "Error: File or directory %-5.*s does not exist\n", 5, name);
        return;
    }

    // If it's a directory, recursively delete contents
    if (superblock.inode[inode_idx].dir_parent & 0x80) {
        for (int i = 0; i < NUM_INODES; i++) {
            if ((superblock.inode[i].used_size & 0x80) &&
                (superblock.inode[i].dir_parent & 0x7F) == inode_idx) {
                fs_delete(superblock.inode[i].name);
            }
        }
    } else {
        // Free blocks
        int size = superblock.inode[inode_idx].used_size & 0x7F;
        mark_blocks(superblock.inode[inode_idx].start_block, size, 0);

        // Zero out blocks
        FILE *disk = fopen(current_disk, "r+b");
        if (!disk) {
            fprintf(stderr, "Error: Cannot open disk %s\n", current_disk);
            return;
        }

        uint8_t zero_block[BLOCK_SIZE] = {0};
        for (int i = 0; i < size; i++) {
            write_block(disk, superblock.inode[inode_idx].start_block + i, zero_block);
        }
        fclose(disk);
    }

    // Zero out inode
    memset(&superblock.inode[inode_idx], 0, sizeof(Inode));

    // Write superblock back to disk
    FILE *disk = fopen(current_disk, "r+b");
    if (!disk) {
        fprintf(stderr, "Error: Cannot open disk %s\n", current_disk);
        return;
    }
    write_block(disk, 0, &superblock);
    fclose(disk);
}

void fs_read(char name[5], int block_num) {
    if (!current_disk) {
        fprintf(stderr, "Error: No file system is mounted\n");
        return;
    }

    int inode_idx = get_file_inode(name, current_dir_inode);
    if (inode_idx == -1 || (superblock.inode[inode_idx].dir_parent & 0x80)) {
        fprintf(stderr, "Error: File %-5.*s does not exist\n", 5, name);
        return;
    }

    int size = superblock.inode[inode_idx].used_size & 0x7F;
    if (block_num < 0 || block_num >= size) {
        fprintf(stderr, "Error: %s does not have block %d\n", name, block_num);
        return;
    }

    FILE *disk = fopen(current_disk, "r");
    read_block(disk, superblock.inode[inode_idx].start_block + block_num, buffer);
    fclose(disk);
}

void fs_write(char name[5], int block_num) {
    if (!current_disk) {
        fprintf(stderr, "Error: No file system is mounted\n");
        return;
    }

    int inode_idx = get_file_inode(name, current_dir_inode);
    if (inode_idx == -1 || (superblock.inode[inode_idx].dir_parent & 0x80)) {
        fprintf(stderr, "Error: File %-5.*s does not exist\n", 5, name);
        return;
    }

    int size = superblock.inode[inode_idx].used_size & 0x7F;
    if (block_num < 0 || block_num >= size) {
        fprintf(stderr, "Error: %s does not have block %d\n", name, block_num);
        return;
    }

    // Open disk for writing
    FILE *disk = fopen(current_disk, "r+b");
    if (!disk) {
        fprintf(stderr, "Error: Cannot open disk %s\n", current_disk);
        return;
    }

    // Calculate actual block number
    int actual_block = superblock.inode[inode_idx].start_block + block_num;

    // Make sure block is marked as used in free block list
    int byte_idx = actual_block / 8;
    int bit_idx = actual_block % 8;
    if (!(superblock.free_block_list[byte_idx] & (1 << bit_idx))) {
        fprintf(stderr, "Error: Attempting to write to an unallocated block\n");
        fclose(disk);
        return;
    }
    // Write updated superblock first
    write_block(disk, 0, &superblock);

    // Write buffer content to the specified block
    write_block(disk, actual_block, buffer);
    fclose(disk);
}

void fs_buff(char buff[1024]) {
    memset(buffer, 0, BLOCK_SIZE); 
    memcpy(buffer, buff, strlen(buff)); 
}

void fs_ls(void) {
    if (!current_disk) {
        fprintf(stderr, "Error: No file system is mounted\n");
        return;
    }

    // Print current directory (.)
    int current_children = 2;
    for (int i = 0; i < NUM_INODES; i++) {
        if ((superblock.inode[i].used_size & 0x80) &&
            (superblock.inode[i].dir_parent & 0x7F) == current_dir_inode) {
            current_children++;
        }
    }
    printf("%-5s %3d\n", ".", current_children);

    // Print parent directory (..)
    int parent_inode = current_dir_inode == 0 ? 0 :
                      (superblock.inode[current_dir_inode].dir_parent & 0x7F);
    int parent_children = 2;
    for (int i = 0; i < NUM_INODES; i++) {
        if ((superblock.inode[i].used_size & 0x80) &&
            (superblock.inode[i].dir_parent & 0x7F) == parent_inode) {
            parent_children++;
        }
    }
    printf("%-5s %3d\n", "..", parent_children);

    // Print all other entries
    for (int i = 0; i < NUM_INODES; i++) {
        if ((superblock.inode[i].used_size & 0x80) &&
            (superblock.inode[i].dir_parent & 0x7F) == current_dir_inode) {
            if (superblock.inode[i].dir_parent & 0x80) {  // Directory
                int dir_children = 2;
                for (int j = 0; j < NUM_INODES; j++) {
                    if ((superblock.inode[j].used_size & 0x80) &&
                        (superblock.inode[j].dir_parent & 0x7F) == i) {
                        dir_children++;
                    }
                }
                printf("%-5.*s %3d\n", 5, superblock.inode[i].name, dir_children);
            } else {  // File
                printf("%-5.*s %3d KB\n", 5, superblock.inode[i].name, superblock.inode[i].used_size & 0x7F);
            }
        }
    }
}

void fs_resize(char name[5], int new_size) {
    if (!current_disk) {
        fprintf(stderr, "Error: No file system is mounted\n");
        return;
    }

    int inode_idx = get_file_inode(name, current_dir_inode);
    if (inode_idx == -1 || (superblock.inode[inode_idx].dir_parent & 0x80)) {
        fprintf(stderr, "Error: File %-5.*s does not exist\n", 5, name);
        return;
    }

    int current_size = superblock.inode[inode_idx].used_size & 0x7F;
    int current_start = superblock.inode[inode_idx].start_block;

    // Open disk once for all operations
    FILE *disk = fopen(current_disk, "r+b");
    if (!disk) {
        fprintf(stderr, "Error: Cannot open disk %s\n", current_disk);
        return;
    }

    if (new_size > current_size) {
        // Check if we can expand in place
        int can_expand = 1;
        for (int i = current_start + current_size;
             i < current_start + new_size && i < NUM_BLOCKS; i++) {
            int byte_idx = i / 8;
            int bit_idx = i % 8;
            if (superblock.free_block_list[byte_idx] & (1 << bit_idx)) {
                can_expand = 0;
                break;
            }
        }

        if (can_expand) {
            // Mark new blocks as used
            mark_blocks(current_start + current_size,
                       new_size - current_size, 1);
        } else {
            // Try to find new location
            int new_start = find_contiguous_blocks(new_size);
            if (new_start == -1) {
                fprintf(stderr, "Error: File %s cannot expand to size %d\n",
                        name, new_size);
                fclose(disk);
                return;
            }

            // Copy data to new location
            uint8_t temp_buffer[BLOCK_SIZE];
            for (int i = 0; i < current_size; i++) {
                read_block(disk, current_start + i, temp_buffer);
                write_block(disk, new_start + i, temp_buffer);
            }

            // Zero out old blocks
            memset(temp_buffer, 0, BLOCK_SIZE);
            for (int i = 0; i < current_size; i++) {
                write_block(disk, current_start + i, temp_buffer);
            }

            // Update block allocation
            mark_blocks(current_start, current_size, 0);
            mark_blocks(new_start, new_size, 1);
            superblock.inode[inode_idx].start_block = new_start;
        }
    } else if (new_size < current_size) {
        // Zero out freed blocks
        uint8_t zero_block[BLOCK_SIZE] = {0};
        for (int i = new_size; i < current_size; i++) {
            write_block(disk, current_start + i, zero_block);
        }

        // Update block allocation
        mark_blocks(current_start + new_size,
                   current_size - new_size, 0);
    }

    // Update size in inode
    superblock.inode[inode_idx].used_size = 0x80 | (new_size & 0x7F);

    // Write superblock back to disk
    write_block(disk, 0, &superblock);
    fclose(disk);
}

void fs_defrag(void) {
    if (!current_disk) {
        fprintf(stderr, "Error: No file system is mounted\n");
        return;
    }

    // Create sorted array of files
    typedef struct {
        int inode_idx;
        int start_block;
        int size;
    } FileInfo;

    FileInfo files[NUM_INODES];
    int num_files = 0;

    for (int i = 0; i < NUM_INODES; i++) {
        if ((superblock.inode[i].used_size & 0x80) &&
            !(superblock.inode[i].dir_parent & 0x80)) {
            files[num_files].inode_idx = i;
            files[num_files].start_block = superblock.inode[i].start_block;
            files[num_files].size = superblock.inode[i].used_size & 0x7F;
            num_files++;
        }
    }

    // Sort files by start block
    for (int i = 0; i < num_files - 1; i++) {
        for (int j = 0; j < num_files - i - 1; j++) {
            if (files[j].start_block > files[j + 1].start_block) {
                FileInfo temp = files[j];
                files[j] = files[j + 1];
                files[j + 1] = temp;
            }
        }
    }

    // Move files toward beginning
    int next_free = 1;  // Start after superblock
    FILE *disk = fopen(current_disk, "r+b");

    for (int i = 0; i < num_files; i++) {
        if (files[i].start_block != next_free) {
            // Read file data
            char *file_data = malloc(files[i].size * BLOCK_SIZE);
            // Move each block of the file
            for (int j = 0; j < files[i].size; j++) {
                read_block(disk, files[i].start_block + j, file_data + j * BLOCK_SIZE);
            }

            // Zero out old blocks
            char zero_block[BLOCK_SIZE] = {0};
            for (int j = 0; j < files[i].size; j++) {
                write_block(disk, files[i].start_block + j, zero_block);
            }

            // Write to new location
            for (int j = 0; j < files[i].size; j++) {
                write_block(disk, next_free + j,
                          file_data + j * BLOCK_SIZE);
            }

            free(file_data);

            // Update inode
            superblock.inode[files[i].inode_idx].start_block = next_free;
        }
        next_free += files[i].size;
    }

    // Update free block list
    memset(superblock.free_block_list, 0, 16);  // Mark all blocks as free
    superblock.free_block_list[0] = 1;  // Mark superblock as used
    for (int i = 0; i < num_files; i++) {
        mark_blocks(superblock.inode[files[i].inode_idx].start_block,
                   files[i].size, 1);
    }

    // Write superblock back to disk
    write_block(disk, 0, &superblock);
    fclose(disk);
}

void fs_cd(char name[5]) {
    if (!current_disk) {
        fprintf(stderr, "Error: No file system is mounted\n");
        return;
    }

    if (strcmp(name, ".") == 0) {
        return; 
    }

    if (strcmp(name, "..") == 0) {
        if (current_dir_inode != 0) {  // Not root directory
            int parent = superblock.inode[current_dir_inode].dir_parent & 0x7F;
            if (parent != 127) {  // Not root
                current_dir_inode = parent;
            }
        }
        return;
    }

    // Find directory in current directory
    int dir_inode = get_file_inode(name, current_dir_inode);
    if (dir_inode == -1 || !(superblock.inode[dir_inode].dir_parent & 0x80)) {
        fprintf(stderr, "Error: Directory %-5.*s does not exist\n", 5, name);
        return;
    }

    current_dir_inode = dir_inode;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <command_file>\n", argv[0]);
        return 1;
    }

    FILE *cmd_file = fopen(argv[1], "r");
    if (!cmd_file) {
        fprintf(stderr, "Error: Cannot open command file %s\n", argv[1]);
        return 1;
    }

    char line[1024];
    int line_num = 0;
    
    while (fgets(line, sizeof(line), cmd_file)) {
        line_num++;
        line[strcspn(line, "\n")] = 0; // Remove newline
        if (line[0] == '\0') continue; // Skip empty lines

        char cmd = line[0];
        char args[1024];
        
        if (strlen(line) > 2) {
            strcpy(args, line + 2);
        } else {
            args[0] = '\0';
        }

        switch (cmd) {
            case 'M': {  // Mount
                char disk_name[256];
                if (sscanf(args, "%s", disk_name) != 1) {
                    fprintf(stderr, "Command Error: %s, %d\n", argv[1], line_num);
                    continue;
                }
                fs_mount(disk_name);
                break;
            }

            case 'C':  // Create
                {
                    char name[6];  // Extra byte for null terminator
                    int size;
                    if (sscanf(line, "C %5s %d", name, &size) != 2 || 
                        size < 0 || size > 127 || strlen(name) > 5) {
                        fprintf(stderr, "Command Error: %s, %d\n", argv[1], line_num);
                        continue;
                    }
                    fs_create(name, size);
                }
                break;

            case 'D':  // Delete
                {
                    char name[6];
                    if (sscanf(line, "D %5s", name) != 1 || strlen(name) > 5) {
                        fprintf(stderr, "Command Error: %s, %d\n", argv[1], line_num);
                        continue;
                    }
                    fs_delete(name);
                }
                break;

            case 'R':  // Read
                {
                    char name[6];
                    int block;
                    if (sscanf(line, "R %5s %d", name, &block) != 2 || 
                        block < 0 || block > 126 || strlen(name) > 5) {
                        fprintf(stderr, "Command Error: %s, %d\n", argv[1], line_num);
                        continue;
                    }
                    fs_read(name, block);
                }
                break;

            case 'W':  // Write
                {
                    char name[6];
                    int block;
                    if (sscanf(line, "W %5s %d", name, &block) != 2 || 
                        block < 0 || block > 126 || strlen(name) > 5) {
                        fprintf(stderr, "Command Error: %s, %d\n", argv[1], line_num);
                        continue;
                    }
                    fs_write(name, block);
                }
                break;

            case 'B':  // Buffer
                {
                    if (strlen(line) < 2) {  // Just "B" 
                        memset(buffer, 0, 1024);
                    } else {
                        char *buffer_content = line + 2;  // Skip "B "
                        if (strlen(buffer_content) > 1024) {
                            fprintf(stderr, "Command Error: %s, %d\n", argv[1], line_num);
                            continue;
                        }
                        fs_buff(buffer_content);
                    }
                }
                break;

            case 'L':  // List
                if (strlen(line) != 1) {
                    fprintf(stderr, "Command Error: %s, %d\n", argv[1], line_num);
                    continue;
                }
                fs_ls();
                break;

            case 'E':  // Resize
                {
                    char name[6];
                    int new_size;
                    if (sscanf(line, "E %5s %d", name, &new_size) != 2 || 
                        new_size <= 0 || new_size > 127 || strlen(name) > 5) {
                        fprintf(stderr, "Command Error: %s, %d\n", argv[1], line_num);
                        continue;
                    }
                    fs_resize(name, new_size);
                }
                break;

            case 'O':  // Defragment
                if (strlen(line) != 1) {
                    fprintf(stderr, "Command Error: %s, %d\n", argv[1], line_num);
                    continue;
                }
                fs_defrag();
                break;

            case 'Y':  // Change directory
                {
                    char name[6];
                    if (sscanf(line, "Y %5s", name) != 1 || strlen(name) > 5) {
                        fprintf(stderr, "Command Error: %s, %d\n", argv[1], line_num);
                        continue;
                    }
                    fs_cd(name);
                }
                break;

            default:
                fprintf(stderr, "Command Error: %s, %d\n", argv[1], line_num);
                break;
        }
    }

    fclose(cmd_file);
    if (current_disk) {
        free(current_disk);
    }
    return 0;
}