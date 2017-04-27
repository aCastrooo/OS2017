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

#define MAGIC_NUM 6942069

//these numbers correspond to blocks of size 512

//size in bytes accounts for 128 inodes
#define IMAP_SIZE 16

//size in bytes accounts for 131072 blocks
#define BMAP_SIZE 16384

//number of inodes
#define INODE_LIST_SIZE 128

//number of blocks of size 512, aka 2^26 bytes, aka 67108864 bytes
#define BLOCK_LIST_SIZE 131072

#define D_BLOCK_START 50

//1 if block n is free, 0 o/w
int isBlockFree(int n){
    char buf[BLOCK_SIZE];
    int blk = 1 + ((n / BLOCK_SIZE) / 8);
    block_read(blk, (void*) buf);

    n = n - (((blk - 1) * BLOCK_SIZE) * 8);

    return (buf[n / 8] >> n % 8) & 1;
}

//sets block n to 1 (free) or 0 (not free)
void setBlock(int n, int setting){
    char buf[BLOCK_SIZE];
    int blk = 1 + ((n / BLOCK_SIZE) / 8);
    block_read(blk, (void*) buf);

    n = n - (((blk - 1) * BLOCK_SIZE) * 8);

    if(setting == 1){
        buf[n / 8] |= 1 << n % 8;
    }else{
        buf[n / 8] &= ~(1 << n % 8);
    }

    block_write(blk, (const void*) buf);
}

//1 if inode n is free, 0 o/w
int isInodeFree(int n){
    char buf[BLOCK_SIZE];
    block_read(0, (void*) buf);

    return (buf[sizeof(int) + (n / 8)] >> n % 8) & 1;
}

//sets inode n to 1 (free) or 0 (not free)
void setInode(int n, int setting){
    char buf[BLOCK_SIZE];
    block_read(0, (void*) buf);

    if(setting == 1){
        buf[sizeof(int) + (n / 8)] |= 1 << n % 8;
    }else{
        buf[sizeof(int) + (n / 8)] &= ~(1 << n % 8);
    }

    block_write(0, (const void*) buf);
}

//returns the nth inode in the list
inode readInode(int n){
    int numPerBlock = BLOCK_SIZE / sizeof(inode);
    int block = 33 + (n / numPerBlock);
    char buf[BLOCK_SIZE];

    block_read(block, (void*) buf);

    inode* rp = (inode*) buf + (n % numPerBlock);
    inode result = *rp;

    return result;
}

void writeInode(inode nd){
    int n = nd.id;
    int numPerBlock = BLOCK_SIZE / sizeof(inode);
    int block = 33 + (n / numPerBlock);
    char buf[BLOCK_SIZE];

    block_read(block, (void*) buf);

    inode* rp = (inode*) buf + (n % numPerBlock);
    *rp = nd;

    block_write(block, (const void*) buf);
}


//checks whether the path to a file is legit by comaring it to iNodes.
//if an iNode exists for the file, returns that inode*
inode checkiNodePathName(const char *path){
    int i;
    for(i = 0; i < INODE_LIST_SIZE; i++){
      	if(!isInodeFree(i)){
            inode in = readInode(i);
            log_msg("path = %s, node's path = %s\n",path, in.path);

      	    if(strcmp(path, in.path) == 0){
      		      return in;
      	    }
      	}
    }

    return (inode) -1;
}

void fillStatBuff(struct stat *statbuf, inode iNode){
    statbuf->st_ino = iNode.id;
    statbuf->st_mode = iNode.mode; //S_IFREG | 0644;
    statbuf->st_nlink = 1;
    statbuf->st_size = iNode.size;
    statbuf->st_blocks = iNode.size / BLOCK_SIZE + 1;

}


int checkIfInit(){
    char buf[BLOCK_SIZE];
    block_read(0, (void *) buf);

    int* magicNumSpace = (int*) buf;

    if(*magicNumSpace == MAGIC_NUM){
        return 1;
    }else{
        return 0;
    }
}

void setFileInit(){
    char buf[BLOCK_SIZE];
    int* magicNumSpace = (int*) buf;
    *magicNumSpace = MAGIC_NUM;

    block_write(0, (const void*) buf);
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

    disk_open(SFS_DATA->diskfile);

    if(checkIfInit() == 1){
        return SFS_DATA;
    }

    setFileInit();

    char buf[BLOCK_SIZE];

    block_read(0, buf);

    //this initializes imap
    int i;
    for (i = 0; i < IMAP_SIZE; i++) {
        buf[i + sizeof(int)] = 0xFF;
    }

    block_write(0, (const void*) buf);
    //magic num and imap span block 0

    //this looks stupid and complicated...and it is,
    //but it sets bmap which spans multiple blocks
    char bmbuf[BMAP_SIZE / BLOCK_SIZE][BLOCK_SIZE];
    int blk = 1;
    i = 0;
    for (blk = 1; blk <= BMAP_SIZE / BLOCK_SIZE; i++) {
        bmbuf[blk][i] = 0xFF;
        if(i % BLOCK_SIZE == 0 && i != 0){
            block_write(blk, (const void*) bmbuf[blk]);
            blk++;
            i = 0;
        }
    }
    //bmap spans block 1-32

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
    log_msg("\nsfs_destroy(userdata=0x%08x)\n", userdata);

    disk_close();
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

    log_msg("\nsfs_getattr(path=\"%s\", statbuf=0x%08x)\n",
	  path, statbuf);

    if(strcmp(path, "/") == 0){
	     statbuf->st_mode = S_IFDIR | 0755;
	     statbuf->st_nlink = 2;
       log_stat(statbuf);

       return retstat;
    }

    inode n = checkiNodePathName(path);
    if(n != (inode) -1){
	     fillStatBuff(statbuf, n);
       log_stat(statbuf);
       return retstat;
    }

    //*statbuf = (struct stat) {0};
    log_stat(statbuf);
    return -1;
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

    int i;
    for(i = 0; i < INODE_LIST_SIZE; i++){
        if(isInodeFree(i) == 1){
            setInode(i, 0);
            inode in = readInode(i);
            in.id = i;
            in.size = 0;
            in.mode = mode;
            in.open = 1;
            in.path = (char*) malloc(256);
            in.data = (int*) malloc(sizeof(int) * 32768);
            writeInode(in);
            //fi->fh = in.id;
            return retstat;
        }
    }


    return retstat;
}

/** Remove a file */
int sfs_unlink(const char *path)
{
    int retstat = 0;
    log_msg("sfs_unlink(path=\"%s\")\n", path);


    inode file = checkiNodePathName(path);
    if(file == -1){
	      return -1;
    }

    //remove a hardlink from the specified inode
    file->hardlink -= 1;
    if(file->hardlink < 1){
      	int i;
      	//set all the blocks that the file uses to free, so other files can use the space if needed
      	for(i = 0; i <= file->size / BLOCK_SIZE; i++){
      	    setBlock(file->data[i], 1);
        }
    }

    free(file->data);
    free(file->path);
    setInode(file->id, 1);

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
    int flags = fcntl(fi->fh, F_GETFL);
    if(flags == -1){
      return -1;
    }
    for(int i = 0; i < INODE_LIST_SIZE; i++){
      if(strcmp(path, SFS_DATA->ilist[i].path) == 0){
        return 0;
      }
    }

    return -1;
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

    log_msg("\nsfs_readdir(path=\"%s\", fi=0x%08x)\n",
	  path, fi);

    char filename[255];

    for(int i = 0; i < INODE_LIST_SIZE; i++){
      if(isInodeFree(i) == 0){
        memcpy(filename, SFS_DATA->ilist[i].path + 1, 255);
        filler(buf, filename, NULL, 0);
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
