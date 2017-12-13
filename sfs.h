#ifndef _SFS_H_
#define _SFS_H_

#include <stdio.h>
#include <stdlib.h>
#include "block.h"

#define FS_FILE "/tmp/ksp98/testfsfile" // location of the disk file
#define PATH_MAX 128 // max length of full file path
#define INODE_NUMBER 30000 // number of inode allowed

/* parse from inode, should not be created directly */
typedef struct pnode_
{
    int mode; // should always be 2
    int direct_blocks[(BLOCK_SIZE/sizeof(int)) - 1]; // indexes of blocks used by file or blocks of pnode
} pnode;

typedef struct inode_
{
    int mode; // 0 for free inode, 1 for inode, 2 for pnode
    uid_t owner; // owner id
    time_t timestamp; // last modified date
    blkcnt_t size_block_count; // number of blocks used by this file
    char file_path[PATH_MAX]; // full path of the file
    int direct_blocks[(BLOCK_SIZE/sizeof(int)) - 4 - sizeof(uid_t) - sizeof(time_t) - sizeof(blkcnt_t) - PATH_MAX/(sizeof(int)/sizeof(char))]; // indexes of blocks used by this file
	int double_direct_blocks; // index of a pnode
    int triple_direct_blocks; // index of a pnode containing a list of another pnode
} inode;

#endif