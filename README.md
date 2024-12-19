# - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
# Name : Longfei Jiao
# SID : 1829457
# CCID : ljiao
# - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

## Program design

The code follows a modular design with clear separation between the file system operations and helper functions. 

### Data structure

#### Disk layout
* Total disk size: 128 KB with 1 KB block size (128 blocks total). 

* Block #0 is the superblock, containing Free-space list (16 bytes bitmap) and 126 inodes for files/directories

#### Inode Structure
* name[5]: 5-character alphanumeric filename  
* used_size: 1 byte (1 bit for state, 7 bits for size)    
* start_block: 1 byte for first block index   
* dir_parent: 1 byte (1 bit for type, 7 bits for parent inode index)  

### Key Operations

* Basic file operations (create, delete, read, write) 
* Directory operations (cd, ls)   
* Maintenance operations (mount, defrag)  
* File manipulation (resize)  

### Features
* Uses contiguous allocation for files
* Implements consistency checking during mount
* Maintains a global buffer for read/write operations
* Supports a hierarchical directory structure
* Read commands from a file


## System Call/C library functions that internally use system calls

1. fs_mount:

* fopen(): Opens disk file for reading
* fseek(): Positions file pointer for reading superblock
* fread(): Reads superblock
* fclose(): Closes disk file
* strdup(): Duplicates disk name string
* memset(): Zeros out buffer

2. fs_create:

* fopen(): Opens disk for writing
* fwrite(): Writes updated superblock
* fclose(): Closes disk file
* memcpy(): Copies file name to inode


3. fs_delete:

* fopen(): Opens disk for read/write
* fseek(): Positions file pointer
* fwrite(): Zeros out blocks
* fseek(): Positions file pointer
* fclose(): Closes disk file
* memset(): Zeros out inode


4. fs_read:

* fopen(): Opens disk for reading
* fseek(): Positions to correct block
* fread(): Reads block into buffer
* fclose(): Closes disk file


5. fs_write:

* fopen(): Opens disk for read/write
* fseek(): Positions to correct block
* fwrite(): Writes buffer to block
* fclose(): Closes disk file


6. fs_buff:

* memset(): Clears buffer
* memcpy(): Copies new data to buffer

7. fs_ls:

* printf(): Prints directory listings


8. fs_resize:

* fopen(): Opens disk for read/write
* fseek(): Positions file pointer
* fread(): Reads blocks
* fwrite(): Writes blocks
* fclose(): Closes disk file
* memset(): Zeros out blocks


9. fs_defrag:

* fopen(): Opens disk for read/write
* fseek(): Positions file pointer
* fread(): Reads blocks
* fwrite(): Writes blocks
* fclose(): Closes disk file
* malloc(): Allocates buffer
* free(): Frees buffer
* memset(): Zeros out blocks


10. main: 
* fopen(): Opens input command file
* fgets(): Reads command lines
* sscanf(): Parses command arguments
* fclose(): Closes input file
* fprintf(): Writes error messages
* strcspn(): Removes newline characters


## Test the program

#### Build the target file

Utilize the "make" command to execute the Makefile script.

#### Copy the target file

Execute the bash script named copy.sh by using command
```
bash copy.sh
```

#### Run tests in each folder

Use the following command to run the program. 
```
./fs input > stdout 2> stderr
```
Compare two text files using the "diff" command
```
diff stdout stdout_expected
```

Compare two disk files:
```
sha256sum disk disk_expected
```

#### Clean up

Use the "git clean" command to clean up test files. 
```
git clean -f
```
Use the "git restore" command to restore previous disks
```
git restore .
```

### Memory Leak Check: 
```
valgrind --tool=memcheck --leak-check=yes ./fs input
```

## Reference: 
Assignment 3 – UNIX File System Simulator Problem Description and Starter Code

[Linux Filesystems API](https://www.kernel.org/doc/html/v4.14/filesystems/index.html)

[How to copy a file to multiple folders using the command line?](https://askubuntu.com/questions/432795/how-to-copy-a-file-to-multiple-folders-using-the-command-line)