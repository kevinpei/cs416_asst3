/*
  Simple File System

  This code is derived from function prototypes found /usr/include/fuse/fuse.h
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
  His code is licensed under the LGPLv2.

*/

#include "params.h"
#include "block.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuse.h>
#include <libgen.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#ifdef HAVE_SYS_XATTR_H
#include <sys/xattr.h>
#endif

#include "log.h"
#include "sfs.h"

static uint64_t file_handle = 0;

//Helper functions

//A function to get the Inode corresponding to the given path.
//On success, returns a pointer to the Inode. On failure, returns NULL.

int getINode(char *path)
{
    char node[BLOCK_SIZE];
    bzero(node, BLOCK_SIZE);
    int i = 0;
    while (i < INODE_NUMBER)
    {
        block_read(i, node);
        Inode *inode = (Inode *)node;
        //Make sure it's an Inode, the file path is the same as the one given
        if ((strncmp(inode->file_path, path, strlen(path)) == 0) && ((strlen(path) == PATH_MAX) || (inode->file_path[strlen(path)] == NULL)))
        {
            return i;
        }
        i++;
    }
    return -1;
}

//A function to get the first free data block
int getFirstFreeBlock()
{
    int currentBlock = INODE_NUMBER + 1;
    char block[BLOCK_SIZE];
    bzero(block, BLOCK_SIZE);
    block_read(currentBlock, block);
    while (((dataNode *)block)->isUsed == 1)
    {
        currentBlock++;
        block_read(currentBlock, block);
    }
    return currentBlock;
}

//A function to get the first free block to make into a Pnode or Inode
int getFirstFreeNode()
{
    int currentBlock = 0;
    char block[BLOCK_SIZE];
    bzero(block, BLOCK_SIZE);
    block_read(currentBlock, block);
    while (((Inode *)block)->mode != 0 && currentBlock < 30000)
    {
        currentBlock++;
        block_read(currentBlock, block);
    }

    if (currentBlock >= 30000)
    {
        printf("No more free nodes\n");
        return -1;
    }
    return currentBlock;
}

void print_inode(const Inode *inode)
{
    int k;
    printf("\ninode:\n------------------\n");
    printf("mode: %d\n", inode->mode);
    printf("owner: %u\n", inode->owner);
    printf("timestamp: %u\n", inode->timestamp);
    printf("file size: %u\n", inode->filesize);
    printf("file path: %s\n", inode->file_path);
    printf("direct blocks:");
    for (k = 0; k < sizeof(inode->direct_blocks) / sizeof(int); k++)
    {
        if (inode->direct_blocks[k] != 0)
        {
            printf("%d, ", inode->direct_blocks[k]);
        }
    }
    printf("\n");
    printf("single indirect blocks: %d\n", inode->single_indirect_blocks);
    printf("double indirect blocks: %d\n", inode->double_indirect_blocks);
    printf("------------------\n");
}

void print_all_inodes()
{
    int k;
    for (k = 0; k < INODE_NUMBER; k++)
    {
        char buffer[BLOCK_SIZE];
        bzero(buffer, BLOCK_SIZE);
        block_read(k, buffer);
        Inode *inode = (Inode *)buffer;
        if (inode->mode == 1)
        {
            print_inode((Inode *)buffer);
        }
    }
}

Inode *get_inode(const char *path)
{
    int k;
    // printf("searching file: %s\n", path);
    Inode *inode = (Inode *)malloc(BLOCK_SIZE);
    for (k = 0; k < INODE_NUMBER; k++)
    {
        block_read(k, inode);
        // print_inode(inode);
        if (inode->mode == 1)
        {
            if ((strncmp(inode->file_path, path, strlen(path)) == 0) && ((strlen(path) == PATH_MAX) || (inode->file_path[strlen(path)] == NULL)))
            {
                // printf("Inode found at block %d\n", k);
                return inode;
            }
        }
    }
    // printf("Inode not found\n");
    free(inode);
    return NULL;
}

int get_file_stat(const Inode *file_info, struct stat *statbuf)
{
    if (file_info->file_path[strlen(file_info->file_path) - 1] == '/')
    {
        statbuf->st_mode = S_IFDIR;
        statbuf->st_blocks = 0;
    }
    else
    {
        statbuf->st_mode = S_IFREG;
        statbuf->st_blocks = file_info->filesize / BLOCK_SIZE + 1;
    }
    statbuf->st_nlink = 1;
    statbuf->st_uid = file_info->owner;
    statbuf->st_gid = fuse_get_context()->gid;
    statbuf->st_size = file_info->filesize;
    statbuf->st_atime = file_info->timestamp;
    statbuf->st_mtime = file_info->timestamp;
    statbuf->st_ctime = file_info->timestamp;

    return 0;
}

///////////////////////////////////////////////////////////
//
// Prototypes for all these functions, and the C-style comments,
// come indirectly from /usr/include/fuse.h
//

/**
 * Initialize filesystem
 *
 * The return value will passed in the private_data field of
 * fuse_context to all file operations and as a parameter to the
 * destroy() method.
 *
 * Introduced in version 2.3
 * Changed in version 2.6
 */
void *sfs_init(struct fuse_conn_info *conn)
{
    fprintf(stderr, "in bb-init\n");
    log_msg("\nsfs_init()\n");

    log_conn(conn);
    log_fuse_context(fuse_get_context());

    disk_open(FS_FILE);

    // init the whole inode area to 0
    int k;
    char buffer[BLOCK_SIZE];
    memset(buffer, 0, BLOCK_SIZE);
    for (k = 0; k < INODE_NUMBER; k++)
    {
        block_write(k, buffer);
    }

    // create inode for root directory(i.e. "/")
    Inode *root = (Inode *)buffer;
    root->mode = 1;
    root->owner = fuse_get_context()->uid;
    root->timestamp = time(NULL);
    root->filesize = 0;
    strcpy(root->file_path, "/");
    block_write(0, root);

    return SFS_DATA;
}

/**
 * Clean up filesystem
 *
 * Called on filesystem exit.
 *
 * Introduced in version 2.3
 */
void sfs_destroy(void *userdata)
{
    disk_open(FS_FILE);
    int k;
    char buffer[BLOCK_SIZE];
    memset(&buffer, 0, BLOCK_SIZE);
    for (k = 0; k < INODE_NUMBER; k++)
    {
        block_write(k, buffer);
    }
    disk_close();

    log_msg("\nsfs_destroy(userdata=0x%08x)\n", userdata);
}

/** Get file attributes.
 *
 * Similar to stat().  The 'st_dev' and 'st_blksize' fields are
 * ignored.  The 'st_ino' field is ignored except if the 'use_ino'
 * mount option is given.
 */
int sfs_getattr(const char *path, struct stat *statbuf)
{
    int retstat = 0;
    char fpath[PATH_MAX];

    log_msg("\nsfs_getattr(path=\"%s\", statbuf=0x%08x)\n",
            path, statbuf);

    if (statbuf->st_mode == S_IFREG)
    {
        printf("it's a file\n");
    }

    if (statbuf->st_mode == S_IFDIR)
    {
        printf("it's a directory\n");
    }

    Inode *file_info = get_inode(path);
    if (file_info == NULL)
    {
        if (path[strlen(path) - 1] == '/')
        {
            sfs_mkdir(path, 777);
        }
        else
        {
            struct fuse_file_info *fi = (struct fuse_file_info *)malloc(sizeof(struct fuse_file_info));
            retstat = sfs_create(path, 777, fi);
            retstat = sfs_release(path, fi);
        }
        if (retstat != 0)
        {
            return retstat;
        }
        else
        {
            return sfs_getattr(path, statbuf);
        }
    }
    else
    {
        retstat = get_file_stat(file_info, statbuf);
        free(file_info);
    }
    return retstat;
}

/**
 * Create and open a file
 *
 * If the file does not exist, first create it with the specified
 * mode, and then open it.
 *
 * If this method is not implemented or under Linux kernel
 * versions earlier than 2.6.15, the mknod() and open() methods
 * will be called instead.
 *
 * Introduced in version 2.5
 */
int sfs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    int retstat = 0;
    log_msg("\nsfs_create(path=\"%s\", mode=0%03o, fi=0x%08x)\n",
            path, mode, fi);

    int firstFreeNode = getFirstFreeNode();
    // printf("index of inode for new file: %d\n", firstFreeNode);
    //No more space to create more files. Operation failed.
    if (firstFreeNode < 0)
    {
        return -1;
    }
    char buffer[BLOCK_SIZE];
    bzero(buffer, BLOCK_SIZE);
    Inode *newfile = (Inode *)buffer;
    newfile->mode = 1;
    newfile->owner = fuse_get_context()->uid;
    newfile->groupid = fuse_get_context()->gid;
    newfile->permissions = mode;
    newfile->timestamp = time(NULL);
    newfile->filesize = 0;
    strcpy(newfile->file_path, path);
    block_write(firstFreeNode, buffer);

    // record the file in the directory
    char dir_name[PATH_MAX];
    bzero(dir_name, PATH_MAX);
    strncpy(dir_name, path, (strrchr(path, '/') - path + 1));
    // printf("create file in directory: %s\n", dir_name);
    Inode *dir_node = get_inode(dir_name);
    int k;
    for (k = 0; k < sizeof(dir_node->direct_blocks) / sizeof(int); k++)
    {
        if (dir_node->direct_blocks[k] == 0)
        {
            dir_node->direct_blocks[k] = firstFreeNode;
            break;
        }
    }
    int dir_node_index = getINode(dir_name);
    // printf("index of inode for directory: %d\n", dir_node_index);
    block_write(dir_node_index, dir_node);
    free(dir_node);

    print_all_inodes();

    if (path[strlen(path) - 1] == '/')
    {
        fi->nonseekable = 1;
        fi->fh = 0;
    }
    else
    {
        fi->nonseekable = 0;
        file_handle++;
        fi->fh = file_handle;
    }
    fi->lock_owner = fuse_get_context()->uid;

    return retstat;
}

/** Remove a file */
int sfs_unlink(const char *path)
{
    int retstat = 0;
    log_msg("sfs_unlink(path=\"%s\")\n", path);

    Inode *inode = get_inode(path);
    int k;
    char buffer[BLOCK_SIZE];
    bzero(buffer, BLOCK_SIZE);
    for (k = 0; k < sizeof(inode->direct_blocks) / sizeof(int); k++)
    {
        if (inode->direct_blocks[k] != 0)
        {
            block_write(inode->direct_blocks[k], buffer);
        }
    }

    if (inode->single_indirect_blocks != 0)
    {
        char buffer[BLOCK_SIZE];
        block_read(inode->single_indirect_blocks, buffer);
        Pnode *direct_pnode = (Pnode *)buffer;
        for (k = 0; k < sizeof(direct_pnode->direct_blocks) / sizeof(int); k++)
        {
            if (direct_pnode->direct_blocks[k] != 0)
            {
                block_write(inode->direct_blocks[k], buffer);
            }
        }
    }

    if (inode->double_indirect_blocks != 0)
    {
        char buffer[BLOCK_SIZE];
        block_read(inode->double_indirect_blocks, buffer);
        Pnode *single_indirect_pnode = (Pnode *)buffer;
        for (k = 0; k < sizeof(single_indirect_pnode->direct_blocks) / sizeof(int); k++)
        {
            if (single_indirect_pnode->direct_blocks[k] != 0)
            {
                char buffer[BLOCK_SIZE];
                block_read(single_indirect_pnode->direct_blocks[k], buffer);
                Pnode *direct_pnode = (Pnode *)buffer;
                for (k = 0; k < sizeof(direct_pnode->direct_blocks) / sizeof(int); k++)
                {
                    if (direct_pnode->direct_blocks[k] != 0)
                    {
                        block_write(inode->direct_blocks[k], buffer);
                    }
                }
            }
        }
    }
    free(inode);

    int inode_index = getINode(path);
    block_write(inode_index, buffer);

    // remove file from directory
    char dir_name[PATH_MAX];
    bzero(dir_name, PATH_MAX);
    strncpy(dir_name, path, (strrchr(path, '/') - path + 1));
    // printf("remove file in directory: %s\n", dir_name);
    Inode *dir_node = get_inode(dir_name);
    for (k = 0; k < sizeof(dir_node->direct_blocks) / sizeof(int); k++)
    {
        if (dir_node->direct_blocks[k] == inode_index)
        {
            dir_node->direct_blocks[k] = 0;
            break;
        }
    }
    int dir_node_index = getINode(dir_name);
    block_write(dir_node_index, dir_node);
    free(dir_node);

    print_all_inodes();

    return retstat;
}

/** File open operation
 *
 * No creation, or truncation flags (O_CREAT, O_EXCL, O_TRUNC)
 * will be passed to open().  Open should check if the operation
 * is permitted for the given flags.  Optionally open may also
 * return an arbitrary filehandle in the fuse_file_info structure,
 * which will be passed to all file operations.
 *
 * Changed in version 2.2
 */
int sfs_open(const char *path, struct fuse_file_info *fi)
{
    int retstat = 0;
    log_msg("\nsfs_open(path\"%s\", fi=0x%08x)\n",
            path, fi);

    Inode *inode = get_inode(path);
    if (inode == NULL)
    {
        fi->nonseekable = 1;
        fi->fh = 0;
    }
    else
    {
        if (path[strlen(path) - 1] == '/')
        {
            fi->nonseekable = 1;
        }
        else
        {
            fi->nonseekable = 0;
        }
        fi->lock_owner = inode->owner;
        if (fi->fh == 0)
        {
            file_handle++;
            fi->fh = file_handle;
        }
    }
    free(inode);
    return retstat;
}

/** Release an open file
 *
 * Release is called when there are no more references to an open
 * file: all file descriptors are closed and all memory mappings
 * are unmapped.
 *
 * For every open() call there will be exactly one release() call
 * with the same flags and file descriptor.  It is possible to
 * have a file opened more than once, in which case only the last
 * release will mean, that no more reads/writes will happen on the
 * file.  The return value of release is ignored.
 *
 * Changed in version 2.2
 */
int sfs_release(const char *path, struct fuse_file_info *fi)
{
    int retstat = 0;
    log_msg("\nsfs_release(path=\"%s\", fi=0x%08x)\n",
            path, fi);

    fi->fh = 0;

    return retstat;
}

/** Read data from an open file
 *
 * Read should return exactly the number of bytes requested except
 * on EOF or error, otherwise the rest of the data will be
 * substituted with zeroes.  An exception to this is when the
 * 'direct_io' mount option is specified, in which case the return
 * value of the read system call will reflect the return value of
 * this operation.
 *
 * Changed in version 2.2
 */
int sfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    int retstat = 0;
    log_msg("\nsfs_read(path=\"%s\", buf=0x%08x, size=%d, offset=%lld, fi=0x%08x)\n",
            path, buf, size, offset, fi);

    //Get the Inode corresponding to the given path.
    int nodeBlock = getINode(path);
    if (nodeBlock < 0)
    {
        log_msg("File not found\n");
        return -1;
    }
    char *node = malloc(BLOCK_SIZE);
    block_read(nodeBlock, node);

    //Which block to read from and where to start in that block
    int block_number = offset / data_size;
	log_msg("block number is %d\n", block_number);
    int block_offset = offset % data_size;

    int size_remaining = size;

    //Repeatedly write into the buffer until there are no more bytes to read
    while (size_remaining > 0)
    {
        dataNode *block = malloc(BLOCK_SIZE);
        //If the block number is greater than the number of blocks in the Inode, go to the first indirect block
        if (block_number > direct_blocks_Inode)
        {
            //If the block number is greater than the number of blocks in the Inode and the first indirect block, go to the second indirect block
            if (block_number > direct_blocks_Inode + direct_blocks_Pnode)
            {
                //Used to determine which Pnode in the double indirect blocks to start at.
                int indirect_Pnode_number = (block_number - direct_blocks_Inode - direct_blocks_Pnode) / direct_blocks_Pnode;
                if (indirect_Pnode_number > direct_blocks_Pnode)
                {
                    //Ran out of memory. No file can be this big. Just return.
                    printf("EOF\n");
                    size_remaining = 0;
                    //Used to determine the block within the Pnode to start at
                }
                else
                {
                    //First, store the indirect Pnode
                    char *first_p_node = malloc(BLOCK_SIZE);
                    block_read(((Inode *)node)->double_indirect_blocks, first_p_node);
                    //Then store the indirect Pnode that's pointed to by that Pnode
                    char *second_p_node = malloc(BLOCK_SIZE);
                    block_read(((Pnode *)first_p_node)->direct_blocks[indirect_Pnode_number], second_p_node);
                    //Select which block to read from the second indirect Pnode
                    int indirect_Pnode_block = (block_number - direct_blocks_Inode - direct_blocks_Pnode) % direct_blocks_Pnode;
                    block_read(((Pnode *)second_p_node)->direct_blocks[indirect_Pnode_block], block);

                    free(second_p_node);
                    free(first_p_node);
                }
            }
            else
            {
                char *p_node = malloc(BLOCK_SIZE);
                block_read(((Inode *)node)->single_indirect_blocks, p_node);
                block_read(((Pnode *)p_node)->direct_blocks[block_number - direct_blocks_Inode], block);
                free(p_node);
            }
        }
        else
        {
            block_read(((Inode *)node)->direct_blocks[block_number], block);
        }
        //If the end of the file hasn't been reached yet, continue as normal.
        int read_size = data_size - block_offset;
		
        //Otherwise, only copy as much data as there is left in the file.
        if (((Inode *)node)->filesize < (block_number * data_size) + data_size)
        {
			log_msg("file size is %d\n", ((Inode *)node)->filesize);
            read_size = ((Inode *)node)->filesize - (block_number * data_size) - block_offset;
            //Stop reading from file.
            size_remaining = 0;
        }
		log_msg("Read size is %d\n", read_size);
        //Copy the data from the data block starting from the offset into the buffer
        //You add sizeof(char) to account for the metadata in each data block.
		log_msg("The block is %s\n", block);
		log_msg("The modified block is %s\n", (char*)block + block_offset + sizeof(char));
        strncpy((char*)buf + retstat, (char*)block + block_offset + sizeof(char), read_size);
		log_msg("The buffer is %s\n", buf);
        //Update the size remaining, bytes written, and the block offset
        size_remaining -= read_size;
        retstat += read_size;
        //Block offset is by default 0
        block_offset = 0;
        block_number++;
        free(block);
    }

    free(node);

    return retstat;
}

/** Write data to an open file
 *
 * Write should return exactly the number of bytes requested
 * except on error.  An exception to this is when the 'direct_io'
 * mount option is specified (see read operation).
 *
 * Changed in version 2.2
 */
int sfs_write(const char *path, const char *buf, size_t size, off_t offset,
	     struct fuse_file_info *fi)
{
    int retstat = 0;
    log_msg("\nsfs_write(path=\"%s\", buf=0x%08x, size=%d, offset=%lld, fi=0x%08x)\n",
	    path, buf, size, offset, fi);
    
    //Get the Inode corresponding to the given path.
	int nodeBlock = getINode(path);
	if (nodeBlock < 0) {
		log_msg("File not found\n");
		return -1;
	}
	char* node = malloc(BLOCK_SIZE);
	disk_open(FS_FILE);
	block_read(nodeBlock, node);
	
	//Which block to write to and where to start in that block
	int block_number = offset/data_size;
	int block_offset = offset%data_size;
	
	int size_remaining = size;
	
	//Have to fill in the empty space if writing to an offset beyond EOF
	if (offset > ((Inode*)node)->filesize) {
		int i = 0;
		while (i < block_number) {
			int offset_amount = data_size;
			if (i == block_number - 1) {
				offset_amount = block_offset;
			}
			if (i > direct_blocks_Inode) {
				if (i > direct_blocks_Inode + direct_blocks_Pnode) {
					int indirect_Pnode_number = (i - direct_blocks_Inode - direct_blocks_Pnode)/direct_blocks_Pnode;
					if(indirect_Pnode_number > direct_blocks_Pnode) {
						//Ran out of memory. No file can be this big. Just return.
						printf("EOF\n");
						size_remaining = 0;
						i = block_number;
					//Used to determine the block within the Pnode to start at
					} else {
						//First, store the indirect Pnode
						char* first_p_node = malloc(BLOCK_SIZE);
						
						if (((Inode*)node)->double_indirect_blocks == 0) {
							Pnode* pnode = malloc(BLOCK_SIZE);
							((Inode*)node)->double_indirect_blocks = getFirstFreeNode();
							pnode->mode = 2;
							block_write(((Inode*)node)->double_indirect_blocks, pnode);
							free(pnode);
						}
						
						block_read(((Inode*)node)->double_indirect_blocks, first_p_node);
						//Then store the indirect Pnode that's pointed to by that Pnode
						char* second_p_node = malloc(BLOCK_SIZE);
						
						if (((Pnode*)first_p_node)->direct_blocks[indirect_Pnode_number] == 0) {
							Pnode* pnode = malloc(BLOCK_SIZE);
							((Pnode*)first_p_node)->direct_blocks[indirect_Pnode_number] = getFirstFreeNode();
							pnode->mode = 2;
							block_write(((Pnode*)first_p_node)->direct_blocks[indirect_Pnode_number], pnode);
							free(pnode);
						}
						
						block_read(((Pnode*)first_p_node)->direct_blocks[indirect_Pnode_number], second_p_node);
						
						//Select which block to read from the second indirect Pnode
						int indirect_Pnode_block = (block_number - direct_blocks_Inode - direct_blocks_Pnode)%direct_blocks_Pnode;
						
						if (((Pnode*)second_p_node)->direct_blocks[indirect_Pnode_block] == 0) {
							dataNode* emptyData = malloc(BLOCK_SIZE);
							emptyData->isUsed = 1;
							((Pnode*)second_p_node)->direct_blocks[indirect_Pnode_block] = getFirstFreeBlock();
							((Inode*)node)->filesize += offset_amount;
							block_write(nodeBlock, node);
							block_write(((Pnode*)second_p_node)->direct_blocks[indirect_Pnode_block], emptyData);
							free(emptyData);
						}
						
						free(second_p_node);
						free(first_p_node);
					}
				} else {
					if (((Inode*)node)->single_indirect_blocks == 0) {
						Pnode* pnode = malloc(BLOCK_SIZE);
						pnode->mode = 2;
						((Inode*)node)->single_indirect_blocks = getFirstFreeNode();
						block_write(((Inode*)node)->single_indirect_blocks, pnode);
						free(pnode);
					}
					
					char* p_node = malloc(BLOCK_SIZE);
					block_read(((Inode*)node)->single_indirect_blocks, p_node);
					
					if (((Pnode*)p_node)->direct_blocks[i - direct_blocks_Inode] == 0) {
						dataNode* emptyData = malloc(BLOCK_SIZE);
						emptyData->isUsed = 1;
						((Pnode*)p_node)->direct_blocks[i - direct_blocks_Inode] = getFirstFreeBlock();
						((Inode*)node)->filesize += offset_amount;
						block_write(nodeBlock, node);
						block_write(((Pnode*)p_node)->direct_blocks[i - direct_blocks_Inode], emptyData);
						free(emptyData);
					}
					free(p_node);
				}
			} else {
				if (((Inode*)node)->direct_blocks[i] == 0) {
					dataNode* emptyData = malloc(BLOCK_SIZE);
					emptyData->isUsed = 1;
					((Inode*)node)->direct_blocks[i] = getFirstFreeBlock();
					((Inode*)node)->filesize += offset_amount;
					block_write(nodeBlock, node);
					block_write(((Inode*)node)->direct_blocks[i], emptyData);
					free(emptyData);
				}
			}
			i++;
		}
	}
	
	
	//Repeatedly write until no bytes are left to be written.
	while (size_remaining > 0) {
		int write_amount = data_size - block_offset;
		if (size_remaining < write_amount) {
			write_amount = size_remaining;
		}
		
		//If the block number is greater than the number of blocks in the Inode, go to the first indirect block
		if (block_number > direct_blocks_Inode) {
			//If the block number is greater than the number of blocks in the Inode and the first indirect block, go to the second indirect block
			if (block_number > direct_blocks_Inode + direct_blocks_Pnode) {
				//Used to determine which Pnode in the double indirect blocks to start at.
				int indirect_Pnode_number = (block_number - direct_blocks_Inode - direct_blocks_Pnode)/direct_blocks_Pnode;
				if(indirect_Pnode_number > direct_blocks_Pnode) {
					//Ran out of memory. No file can be this big. Just return.
					log_msg("EOF\n");
					size_remaining = 0;
				//Used to determine the block within the Pnode to start at
				} else {
					//First, store the indirect Pnode
					char* first_p_node = malloc(BLOCK_SIZE);
					
					if (((Inode*)node)->double_indirect_blocks == 0) {
						Pnode* pnode = malloc(BLOCK_SIZE);
						((Inode*)node)->double_indirect_blocks = getFirstFreeNode();
						pnode->mode = 2;
						block_write(((Inode*)node)->double_indirect_blocks, pnode);
						free(pnode);
					}
					
					block_read(((Inode*)node)->double_indirect_blocks, first_p_node);
					//Then store the indirect Pnode that's pointed to by that Pnode
					char* second_p_node = malloc(BLOCK_SIZE);
					
					if (((Pnode*)first_p_node)->direct_blocks[indirect_Pnode_number] == 0) {
						Pnode* pnode = malloc(BLOCK_SIZE);
						((Pnode*)first_p_node)->direct_blocks[indirect_Pnode_number] = getFirstFreeNode();
						pnode->mode = 2;
						block_write(((Pnode*)first_p_node)->direct_blocks[indirect_Pnode_number], pnode);
						free(pnode);
					}
					
					block_read(((Pnode*)first_p_node)->direct_blocks[indirect_Pnode_number], second_p_node);
					
					//Select which block to read from the second indirect Pnode
					int indirect_Pnode_block = (block_number - direct_blocks_Inode - direct_blocks_Pnode)%direct_blocks_Pnode;
					
					if (((Pnode*)second_p_node)->direct_blocks[indirect_Pnode_block] == 0) {
						((Pnode*)second_p_node)->direct_blocks[indirect_Pnode_block] = getFirstFreeBlock();
					}
					
					if (block_number * data_size + block_offset + write_amount > ((Inode*)node)->filesize) {
						((Inode*)node)->filesize = block_number * data_size + block_offset + write_amount;
						log_msg("New filesize is %d\n", ((Inode*)node)->filesize);
						block_write(nodeBlock, node);
					}
					
					dataNode* block = malloc(BLOCK_SIZE);
					block_read(((Pnode*)second_p_node)->direct_blocks[indirect_Pnode_block], block);
					block->isUsed = 1;
					strncpy((char*)block->data + block_offset, buf + (size - size_remaining), write_amount);
					block_write(((Pnode*)second_p_node)->direct_blocks[indirect_Pnode_block], block);
					free(block);
					
					free(second_p_node);
					free(first_p_node);
				}
			} else {
				
				if (((Inode*)node)->single_indirect_blocks == 0) {
					Pnode* pnode = malloc(BLOCK_SIZE);
					pnode->mode = 2;
					((Inode*)node)->single_indirect_blocks = getFirstFreeNode();
					block_write(((Inode*)node)->single_indirect_blocks, pnode);
					free(pnode);
				}
				
				char* p_node = malloc(BLOCK_SIZE);
				block_read(((Inode*)node)->single_indirect_blocks, p_node);
				
				if (((Pnode*)p_node)->direct_blocks[block_number - direct_blocks_Inode] == 0) {
					((Pnode*)p_node)->direct_blocks[block_number - direct_blocks_Inode] = getFirstFreeBlock();
				}
				
				if (block_number * data_size + block_offset + write_amount > ((Inode*)node)->filesize) {
					((Inode*)node)->filesize = block_number * data_size + block_offset + write_amount;
					log_msg("New filesize is %d\n", ((Inode*)node)->filesize);
					block_write(nodeBlock, node);
				}
				
				dataNode* block = malloc(BLOCK_SIZE);
				block_read(((Pnode*)p_node)->direct_blocks[block_number - direct_blocks_Inode], block);
				block->isUsed = 1;
				strncpy((char*)block->data + block_offset, buf + (size - size_remaining), write_amount);
				block_write(((Pnode*)p_node)->direct_blocks[block_number - direct_blocks_Inode], block);
				free(block);
				free(p_node);
			}
		} else {
			//Read the currently stored block and write over it.
			dataNode* block = malloc(BLOCK_SIZE);
			if (((Inode*)node)->direct_blocks[block_number] == 0) {
				((Inode*)node)->direct_blocks[block_number] = getFirstFreeBlock();
			}
			
			if (block_number * data_size + block_offset + write_amount > ((Inode*)node)->filesize) {
				((Inode*)node)->filesize = block_number * data_size + block_offset + write_amount;
				log_msg("New filesize is %d\n", ((Inode*)node)->filesize);
				block_write(nodeBlock, node);
			}
			
			block_read(((Inode*)node)->direct_blocks[block_number], block);
			block->isUsed = 1;
			strncpy((char*)block->data + block_offset, buf + (size - size_remaining), write_amount);
			block_write(((Inode*)node)->direct_blocks[block_number], block);
			free(block);
			
			dataNode* test = malloc(BLOCK_SIZE);
			block_read(((Inode*)node)->direct_blocks[block_number], test);
			log_msg("Test %s\n", test);
			free(test);
		}
		block_offset = 0;
		size_remaining -=write_amount;
		retstat += write_amount;
		block_number++;
	}
	
	log_msg("wrote %d bytes \n", retstat);
    return retstat;
}

/** Create a directory */
int sfs_mkdir(const char *path, mode_t mode)
{
    int retstat = 0;
    log_msg("\nsfs_mkdir(path=\"%s\", mode=0%3o)\n",
            path, mode);

    int firstFreeNode = getFirstFreeNode();
    // printf("index of inode for new file: %d\n", firstFreeNode);
    //No more space to create more files. Operation failed.
    if (firstFreeNode < 0)
    {
        return -1;
    }
    char buffer[BLOCK_SIZE];
    bzero(buffer, BLOCK_SIZE);
    Inode *newfile = (Inode *)buffer;
    newfile->mode = 1;
    newfile->owner = fuse_get_context()->uid;
    newfile->groupid = fuse_get_context()->gid;
    newfile->permissions = mode;
    newfile->timestamp = time(NULL);
    newfile->filesize = 0;
    strcpy(newfile->file_path, path);
    block_write(firstFreeNode, buffer);

    // record the file in the directory
    char dir_name[PATH_MAX];
    bzero(dir_name, PATH_MAX);
    strncpy(dir_name, path, (strrchr(path, '/') - path + 1));
    // printf("create file in directory: %s\n", dir_name);
    Inode *dir_node = get_inode(dir_name);
    int k;
    for (k = 0; k < sizeof(dir_node->direct_blocks) / sizeof(int); k++)
    {
        if (dir_node->direct_blocks[k] == 0)
        {
            dir_node->direct_blocks[k] = firstFreeNode;
            break;
        }
    }
    int dir_node_index = getINode(dir_name);
    // printf("index of inode for directory: %d\n", dir_node_index);
    block_write(dir_node_index, dir_node);
    free(dir_node);

    print_all_inodes();

    return retstat;
}

/** Remove a directory */
int sfs_rmdir(const char *path)
{
    int retstat = 0;
    log_msg("sfs_rmdir(path=\"%s\")\n",
            path);

    Inode *inode = get_inode(path);
    int k;
    char buffer[BLOCK_SIZE];
    bzero(buffer, BLOCK_SIZE);
    for (k = 0; k < sizeof(inode->direct_blocks) / sizeof(int); k++)
    {
        if (inode->direct_blocks[k] != 0)
        {
            printf("only empty directory can be removed\n");
            return -1;
        }
    }

    if (inode->single_indirect_blocks != 0)
    {
        printf("only empty directory can be removed\n");
        return -1;
    }

    if (inode->double_indirect_blocks != 0)
    {
        printf("only empty directory can be removed\n");
        return -1;
    }
    free(inode);

    int inode_index = getINode(path);
    block_write(inode_index, buffer);

    // remove file from directory
    char dir_name[PATH_MAX];
    bzero(dir_name, PATH_MAX);
    strncpy(dir_name, path, (strrchr(path, '/') - path + 1));
    // printf("remove file in directory: %s\n", dir_name);
    Inode *dir_node = get_inode(dir_name);
    for (k = 0; k < sizeof(dir_node->direct_blocks) / sizeof(int); k++)
    {
        if (dir_node->direct_blocks[k] == inode_index)
        {
            dir_node->direct_blocks[k] = 0;
            break;
        }
    }
    int dir_node_index = getINode(dir_name);
    block_write(dir_node_index, dir_node);
    free(dir_node);

    print_all_inodes();

    return retstat;
}

/** Open directory
 *
 * This method should check if the open operation is permitted for
 * this  directory
 *
 * Introduced in version 2.3
 */
int sfs_opendir(const char *path, struct fuse_file_info *fi)
{
    int retstat = 0;
    log_msg("\nsfs_opendir(path=\"%s\", fi=0x%08x)\n",
            path, fi);

    return retstat;
}

/** Read directory
 *
 * This supersedes the old getdir() interface.  New applications
 * should use this.
 *
 * The filesystem may choose between two modes of operation:
 *
 * 1) The readdir implementation ignores the offset parameter, and
 * passes zero to the filler function's offset.  The filler
 * function will not return '1' (unless an error happens), so the
 * whole directory is read in a single readdir operation.  This
 * works just like the old getdir() method.
 *
 * 2) The readdir implementation keeps track of the offsets of the
 * directory entries.  It uses the offset parameter and always
 * passes non-zero offset to the filler function.  When the buffer
 * is full (or an error happens) the filler function will return
 * '1'.
 *
 * Introduced in version 2.3
 */
int sfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset,
                struct fuse_file_info *fi)
{
    int retstat = 0;
    log_msg("\nsfs_readdir(path=\"%s\", buf=0x%08x, filler=0x%08x, offset=%lld, fi=0x%08x)\n",
            path, buf, filler, offset, fi);

    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    Inode *inode = get_inode(path);
    int k;
    if (inode == NULL)
    {
        printf("directory \"%s\" does not exist\n", path);
        return retstat;
    }
    // printf("about to iterate direct blocks\n");
    // getchar();
    for (k = 0; k < sizeof(inode->direct_blocks) / sizeof(int); k++)
    {
        if (inode->direct_blocks[k] != 0)
        {
            char buffer[BLOCK_SIZE];
            bzero(buffer, BLOCK_SIZE);
            block_read(inode->direct_blocks[k], buffer);
            Inode *file_info = (Inode *)buffer;
            // printf("file found. print inode:\n");
            // print_inode(file_info);
            char statbuf[sizeof(struct stat)];
            bzero(statbuf, sizeof(struct stat));
            retstat = get_file_stat(file_info, statbuf);
            // printf("about to fill from direct blocks\n");
            // getchar();
            char file_name[PATH_MAX];
            bzero(file_name, PATH_MAX);
            strncpy(file_name, file_info->file_path + strlen(path), strlen(file_info->file_path) - strlen(path));
            printf("about to fill file \"%s\"\n", file_name);
            int retval = filler(buf, file_name, NULL, offset);
            if (retval == 1)
            {
                printf("buffer is full\n");
                break;
            }
        }
    }
    // printf("finish filling direct blocks\n");

    if (inode->single_indirect_blocks != 0)
    {
        char direct_pnode_buffer[BLOCK_SIZE];
        bzero(direct_pnode_buffer, BLOCK_SIZE);
        block_read(inode->single_indirect_blocks, direct_pnode_buffer);
        Pnode *direct_pnode = (Pnode *)direct_pnode_buffer;
        for (k = 0; k < sizeof(direct_pnode->direct_blocks) / sizeof(int); k++)
        {
            if (direct_pnode->direct_blocks[k] != 0)
            {
                char buffer[BLOCK_SIZE];
                bzero(buffer, BLOCK_SIZE);
                block_read(inode->direct_blocks[k], buffer);
                Inode *file_info = (Inode *)buffer;
                // printf("file found. print inode:\n");
                // print_inode(file_info);
                char statbuf[sizeof(struct stat)];
                bzero(statbuf, sizeof(struct stat));
                retstat = get_file_stat(file_info, statbuf);
                // printf("about to fill from direct blocks\n");
                // getchar();
                char file_name[PATH_MAX];
                bzero(file_name, PATH_MAX);
                char *name = strrchr(path, '/') + 1;
                // strcpy(file_name, ".");
                strncpy(file_name, name, strlen(file_info->file_path) - strlen(path));
                int retval = filler(buf, file_name, NULL, offset);
                if (retval == 1)
                {
                    printf("buffer is full\n");
                    break;
                }
            }
        }
    }

    if (inode->double_indirect_blocks != 0)
    {
        char single_indirect_pnode_buffer[BLOCK_SIZE];
        bzero(single_indirect_pnode_buffer, BLOCK_SIZE);
        block_read(inode->double_indirect_blocks, single_indirect_pnode_buffer);
        Pnode *single_indirect_pnode = (Pnode *)single_indirect_pnode_buffer;
        for (k = 0; k < sizeof(single_indirect_pnode->direct_blocks) / sizeof(int); k++)
        {
            if (single_indirect_pnode->direct_blocks[k] != 0)
            {
                char direct_pnode_buffer[BLOCK_SIZE];
                bzero(direct_pnode_buffer, BLOCK_SIZE);
                block_read(single_indirect_pnode->direct_blocks[k], direct_pnode_buffer);
                Pnode *direct_pnode = (Pnode *)direct_pnode_buffer;
                for (k = 0; k < sizeof(direct_pnode->direct_blocks) / sizeof(int); k++)
                {
                    if (direct_pnode->direct_blocks[k] != 0)
                    {
                        char buffer[BLOCK_SIZE];
                        bzero(buffer, BLOCK_SIZE);
                        block_read(inode->direct_blocks[k], buffer);
                        Inode *file_info = (Inode *)buffer;
                        // printf("file found. print inode:\n");
                        // print_inode(file_info);
                        char statbuf[sizeof(struct stat)];
                        bzero(statbuf, sizeof(struct stat));
                        retstat = get_file_stat(file_info, statbuf);
                        // printf("about to fill from direct blocks\n");
                        // getchar();
                        char file_name[PATH_MAX];
                        bzero(file_name, PATH_MAX);
                        char *name = strrchr(path, '/') + 1;
                        // strcpy(file_name, ".");
                        strncpy(file_name, name, strlen(file_info->file_path) - strlen(path));
                        int retval = filler(buf, file_name, NULL, offset);
                        if (retval == 1)
                        {
                            printf("buffer is full\n");
                            break;
                        }
                    }
                }
            }
        }
    }
    free(inode);
    return retstat;
}

/** Release directory
 *
 * Introduced in version 2.3
 */
int sfs_releasedir(const char *path, struct fuse_file_info *fi)
{
    int retstat = 0;
    log_msg("\nsfs_releasedir(path=\"%s\", fi=0x%08x)\n",
            path, fi);

    fi->fh = 0;

    return retstat;
}

struct fuse_operations sfs_oper = {
    .init = sfs_init,
    .destroy = sfs_destroy,

    .getattr = sfs_getattr,
    .create = sfs_create,
    .unlink = sfs_unlink,
    .open = sfs_open,
    .release = sfs_release,
    .read = sfs_read,
    .write = sfs_write,

    .rmdir = sfs_rmdir,
    .mkdir = sfs_mkdir,

    .opendir = sfs_opendir,
    .readdir = sfs_readdir,
    .releasedir = sfs_releasedir};

void sfs_usage()
{
    fprintf(stderr, "usage:  sfs [FUSE and mount options] diskFile mountPoint\n");
    abort();
}

int main(int argc, char *argv[])
{
    int fuse_stat;
    struct sfs_state *sfs_data;

    // sanity checking on the command line
    if ((argc < 3) || (argv[argc - 2][0] == '-') || (argv[argc - 1][0] == '-'))
        sfs_usage();

    sfs_data = malloc(sizeof(struct sfs_state));
    if (sfs_data == NULL)
    {
        perror("main calloc");
        abort();
    }

    // Pull the diskfile and save it in internal data
    sfs_data->diskfile = argv[argc - 2];
    argv[argc - 2] = argv[argc - 1];
    argv[argc - 1] = NULL;
    argc--;

    sfs_data->logfile = log_open();

    // turn over control to fuse
    fprintf(stderr, "about to call fuse_main, %s \n", sfs_data->diskfile);
    fuse_stat = fuse_main(argc, argv, &sfs_oper, sfs_data);
    fprintf(stderr, "fuse_main returned %d\n", fuse_stat);

    return fuse_stat;
}
