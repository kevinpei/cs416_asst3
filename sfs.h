#ifndef _SFS_H_
#define _SFS_H_

#include <stdio.h>
#include <stdlib.h>
#include "block.h"

#define FS_FILE "/tmp/ksp98/testfsfile" // location of the disk file
#define PATH_MAX 256 // max length of full file path
#define INODE_NUMBER 30000 // number of inode allowed
//Defining the size of various things
#define direct_blocks_Inode ((BLOCK_SIZE - 4 * sizeof(int) - sizeof(mode_t) - sizeof(size_t) - sizeof(uid_t) - sizeof(gid_t) - sizeof(time_t) - PATH_MAX * sizeof(char)) / sizeof(int))
#define direct_blocks_Pnode ((BLOCK_SIZE/sizeof(int)) - 1)
#define data_size ((BLOCK_SIZE - sizeof(char)) / sizeof(char))

/* parse from inode, should not be created directly */
typedef struct pnode_
{
    int mode; // should always be 2
    int direct_blocks[direct_blocks_Pnode]; // indexes of blocks used by file or blocks of pnode
} Pnode;

typedef struct inode_
{
    int mode; // 0 for free inode, 1 for inode, 2 for pnode
	mode_t permissions; //The permissions this file has
    uid_t owner; // owner id
	gid_t groupid; //Group id
    time_t timestamp; // last modified date
	size_t filesize;
    char file_path[PATH_MAX]; // full path of the file
    int direct_blocks[direct_blocks_Inode]; // indexes of blocks used by this file
	int single_indirect_blocks; // index of a pnode
    int double_indirect_blocks; // index of a pnode containing a list of another pnode
} Inode;

typedef struct dataNode_
{
	char isUsed; //0 for free data, 1 for used data
	char data[data_size]; //The rest of the bytes are for storing actual data
} dataNode;



#endif