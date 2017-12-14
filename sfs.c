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

//Helper functions

//A function to get the Inode corresponding to the given path.
//On success, returns a pointer to the Inode. On failure, returns NULL.

int getINode(char* path) {
	char* node = malloc(BLOCK_SIZE);
	int i = 0;
	disk_open(FS_FILE);
	while (i < INODE_NUMBER) {
		block_read(i, node);
		//Make sure it's an Inode, the file path is the same as the one given, and that the owner is the current user
		if (((Inode*)node)->mode == 1 && strncmp(((Inode*)node)->file_path, path, PATH_MAX) == 0 && ((Inode*)node)->owner == getuid()&& ((Inode*)node)->groupid == getgid()) {
			free(node);
			disk_close();
			return i;
		}
		i++;
	}
	free(node);
	disk_close();
	return -1;
}

//A function to get the first free data block
int getFirstFreeBlock() {
	int currentBlock = INODE_NUMBER + 1;
	char* block = malloc(BLOCK_SIZE);
	disk_open(FS_FILE);
	block_read(currentBlock, block);
	while (((dataNode*)block)->isFree == 1) {
		currentBlock++;
		block_read(currentBlock, block);
	}
	disk_close();
	free(block);
	return currentBlock;
}

//A function to get the first free block to make into a Pnode or Inode
int getFirstFreeNode() {
	int currentBlock = 0;
	char* block = malloc(BLOCK_SIZE);
	disk_open(FS_FILE);
	block_read(currentBlock, block);
	while (((Inode*)block)->mode != 0 && currentBlock < 30000) {
		currentBlock++;
		block_read(currentBlock, block);
	}
	disk_close();
	free(block);
	if (currentBlock >= 30000) {
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
    for (k = 0; k < sizeof(inode->direct_blocks)/sizeof(int); k++)
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

Inode * get_inode(const char *path)
{
    disk_open(FS_FILE);
    int k;
    // printf("searching file: %s\n", path);
    Inode *inode = (Inode *)malloc(BLOCK_SIZE);
    for (k = 0; k < INODE_NUMBER; k++)
    {
	block_read(k, inode);
	// print_inode(inode);
	if (inode->mode == 1)
	{
	    if ((strncmp(inode->file_path, path, strlen(path)) == 0)
		&& ((strlen(path) == PATH_MAX)|| (inode->file_path[strlen(path)] == NULL)))
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
	statbuf->st_blocks = file_info->filesize/BLOCK_SIZE+1;
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

    Inode *file_info = get_inode(path);
    if (file_info == NULL)
    {
	return NULL;
    }

    retstat = get_file_stat(file_info, statbuf);
    
    free(file_info);
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
    
	int InodeBlock = getINode(path);
	//If the file doesn't already exist, then create a new one and write it in the file blocks.
	if (InodeBlock == -1) {
		int firstFreeNode = getFirstFreeNode();
		
		//No more space to create more files. Operation failed.
		if (firstFreeNode < 0) {
			return -1;
		}
		Inode* newfile = malloc(BLOCK_SIZE);
		newfile->mode = 1;
		newfile->owner = getuid();
		newfile->groupid = getgid();
		newfile->permissions = mode;
		newfile->timestamp = time( NULL );
		newfile->size_block_count = 0;
		newfile->filesize = 0;
		strncpy(newfile->file_path, path, PATH_MAX); 
		
		disk_open(FS_FILE);
		block_write(firstFreeNode, newfile);
		disk_close();
		free(newfile);
	}
	
	sfs_open(path, fi);
    
    return retstat;
}

/** Remove a file */
int sfs_unlink(const char *path)
{
    int retstat = 0;
    log_msg("sfs_unlink(path=\"%s\")\n", path);

    
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
	if (nodeBlock < 0) {
		printf("File not found\n");
		return -1;
	}
	char* node = malloc(BLOCK_SIZE);
	disk_open(FS_FILE);
	block_read(nodeBlock, node);
	
	//Which block to read from and where to start in that block
	int block_number = offset/data_size;
	int block_offset = offset%data_size;
	
	int size_remaining = size;
	
	//Repeatedly write into the buffer until there are no more bytes to read
	while (size_remaining > 0) {
		char* block = malloc(BLOCK_SIZE);
		block_read(nodeBlock, node);
		//If the block number is greater than the number of blocks in the Inode, go to the first indirect block
		if (block_number > direct_blocks_Inode) {
			//If the block number is greater than the number of blocks in the Inode and the first indirect block, go to the second indirect block
			if (block_number > direct_blocks_Inode + direct_blocks_Pnode) {
				//Used to determine which Pnode in the double indirect blocks to start at.
				int indirect_Pnode_number = (block_number - direct_blocks_Inode - direct_blocks_Pnode)/direct_blocks_Pnode;
				if(indirect_Pnode_number > direct_blocks_Pnode) {
					//Ran out of memory. No file can be this big. Just return.
					printf("EOF\n");
					size_remaining = 0;
				//Used to determine the block within the Pnode to start at
				} else {
					//First, store the indirect Pnode
					char* first_p_node = malloc(BLOCK_SIZE);
					block_read(((Inode*)node)->double_indirect_blocks, first_p_node);
					//Then store the indirect Pnode that's pointed to by that Pnode
					char* second_p_node = malloc(BLOCK_SIZE);
					block_read(((Pnode*)first_p_node)->direct_blocks[indirect_Pnode_number], second_p_node);
					//Select which block to read from the second indirect Pnode
					int indirect_Pnode_block = (block_number - direct_blocks_Inode - direct_blocks_Pnode)%direct_blocks_Pnode;
					block_read(((Pnode*)second_p_node)->direct_blocks[indirect_Pnode_block], block);
					
					free(second_p_node);
					free(first_p_node);
				}
			} else {
				char* p_node = malloc(BLOCK_SIZE);
				block_read(((Inode*)node)->single_indirect_blocks, p_node);
				block_read(((Pnode*)p_node)->direct_blocks[block_number - direct_blocks_Inode], block);
				free(p_node);
			}
		} else {
			block_read(((Inode*)node)->direct_blocks[block_number], block);
		}
		//If the end of the file hasn't been reached yet, continue as normal.
		int file_remaining = data_size - block_offset;
		
		//Otherwise, only copy as much data as there is left in the file.
		if (((Inode*)node)->filesize < (block_number * data_size) + data_size) {
			file_remaining = ((Inode*)node)->filesize - (block_number * data_size) - block_offset;
			//Stop reading from file.
			size_remaining = 0;
		}
		
		//Copy the data from the data block starting from the offset into the buffer
		//You add sizeof(char) to account for the metadata in each data block.
		strncpy(buf + retstat, block + block_offset + sizeof(char), file_remaining);
		//Update the size remaining, bytes written, and the block offset
		size_remaining -= file_remaining;
		retstat += file_remaining;
		//Block offset is by default 0
		block_offset = 0;
		block_number++;
		free(block);
	}
	disk_close();
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
    
    
    return retstat;
}


/** Create a directory */
int sfs_mkdir(const char *path, mode_t mode)
{
    int retstat = 0;
    log_msg("\nsfs_mkdir(path=\"%s\", mode=0%3o)\n",
	    path, mode);
   
    
    return retstat;
}


/** Remove a directory */
int sfs_rmdir(const char *path)
{
    int retstat = 0;
    log_msg("sfs_rmdir(path=\"%s\")\n",
	    path);
    
    
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
    
    Inode *inode = get_inode(path);
    int k;
    for (k = 0; k < sizeof(inode->direct_blocks)/sizeof(int); k++)
    {
	if (inode->direct_blocks[k] != 0)
	{
	    char buffer[BLOCK_SIZE];
	    block_read(inode->direct_blocks[k], buffer);
	    Inode *file_info = (Inode *)buffer;
	    struct stat *statbuf;
	    retstat = get_file_stat(file_info, statbuf);
	    filler(buf, path, statbuf, 0);
	}
    }

    if (inode->single_indirect_blocks != 0)
    {
	char buffer[BLOCK_SIZE];
	block_read(inode->single_indirect_blocks, buffer);
	Pnode *direct_pnode = (Pnode *)buffer;
	for (k = 0; k < sizeof(direct_pnode->direct_blocks)/sizeof(int); k++)
	{
	    if (direct_pnode->direct_blocks[k] != 0)
	    {
		char buffer[BLOCK_SIZE];
		block_read(direct_pnode->direct_blocks[k], buffer);
		Inode *file_info = (Inode *)buffer;
		struct stat *statbuf;
		retstat = get_file_stat(file_info, statbuf);
		filler(buf, path, statbuf, 0);
	    }
	}
    }

    if (inode->double_indirect_blocks != 0)
    {
	char buffer[BLOCK_SIZE];
	block_read(inode->double_indirect_blocks, buffer);
	Pnode *single_indirect_pnode = (Pnode *)buffer;
	for (k = 0; k < sizeof(single_indirect_pnode->direct_blocks)/sizeof(int); k++)
	{
	    if (single_indirect_pnode->direct_blocks[k] != 0)
	    {
		char buffer[BLOCK_SIZE];
		block_read(single_indirect_pnode->direct_blocks[k], buffer);
		Pnode *direct_pnode = (Pnode *)buffer;
		for (k = 0; k < sizeof(direct_pnode->direct_blocks)/sizeof(int); k++)
		{
		    if (direct_pnode->direct_blocks[k] != 0)
		    {
			char buffer[BLOCK_SIZE];
			block_read(direct_pnode->direct_blocks[k], buffer);
			Inode *file_info = (Inode *)buffer;
			struct stat *statbuf;
			retstat = get_file_stat(file_info, statbuf);
			filler(buf, path, statbuf, 0);
		    }
		}
	    }
	}
    }
    
    return retstat;
}

/** Release directory
 *
 * Introduced in version 2.3
 */
int sfs_releasedir(const char *path, struct fuse_file_info *fi)
{
    int retstat = 0;

    
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
  .releasedir = sfs_releasedir
};

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
    if ((argc < 3) || (argv[argc-2][0] == '-') || (argv[argc-1][0] == '-'))
	sfs_usage();

    sfs_data = malloc(sizeof(struct sfs_state));
    if (sfs_data == NULL) {
	perror("main calloc");
	abort();
    }

    // Pull the diskfile and save it in internal data
    sfs_data->diskfile = argv[argc-2];
    argv[argc-2] = argv[argc-1];
    argv[argc-1] = NULL;
    argc--;
    
    sfs_data->logfile = log_open();
    
    // turn over control to fuse
    fprintf(stderr, "about to call fuse_main, %s \n", sfs_data->diskfile);
    fuse_stat = fuse_main(argc, argv, &sfs_oper, sfs_data);
    fprintf(stderr, "fuse_main returned %d\n", fuse_stat);
    
    return fuse_stat;
}
