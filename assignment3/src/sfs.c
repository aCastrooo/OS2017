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
#include <time.h>
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

//size in bytes accounts for 4096 blocks
#define BMAP_SIZE 512

//number of inodes
#define INODE_LIST_SIZE 128

//number of blocks of size 16384, aka 2^26 bytes, aka 67108864 bytes
#define BLOCK_LIST_SIZE 4096

#define D_BLOCK_START 50

//1 if block n is free, 0 o/w
int isBlockFree(int n){
    char buf[BLOCK_SIZE];
    //int blk = 1 + ((n / BLOCK_SIZE) / 8);
    block_read(1, (void*) buf);


    //n = n - (((blk - 1) * BLOCK_SIZE) * 8);

    unsigned result = (buf[n / 8] >> n % 8) & 0x1;

    return result;
}

//sets block n to 1 (free) or 0 (not free)
void setBlock(int n, int setting){
    char buf[BLOCK_SIZE];
    //int blk = 1 + ((n / BLOCK_SIZE) / 8);
    block_read(1, (void*) buf);



    //n = n - (((blk - 1) * BLOCK_SIZE) * 8);
    char c = 1;
    if(setting == 1){

        buf[n / 8] |= c << n % 8;
    }else{
        buf[n / 8] &= ~(c << n % 8);
    }

    block_write(1, (const void*) buf);


}

//1 if inode n is free, 0 o/w
int isInodeFree(int n){
    char buf[BLOCK_SIZE];
    block_read(0, (void*) buf);

    unsigned result = (buf[sizeof(int) + (n / 8)] >> n % 8) & 0x1;

    return result;
}

//sets inode n to 1 (free) or 0 (not free)
void setInode(int n, int setting){
    char buf[BLOCK_SIZE];
    block_read(0, (void*) buf);


    char c = 1;
    if(setting == 1){
        buf[sizeof(int) + (n / 8)] |= c << n % 8;
    }else{
        buf[sizeof(int) + (n / 8)] &= ~(c << n % 8);
    }

    block_write(0, (const void*) buf);


}

//returns the nth inode in the list
inode readInode(int n){
    int numPerBlock = BLOCK_SIZE / sizeof(inode);
    int block = 3 + (n / numPerBlock);
    char buf[BLOCK_SIZE];

    block_read(block, (void*) buf);



    inode* rp = (inode*) (buf + (n % numPerBlock) * sizeof(inode));
    inode result = *rp;

    return result;
}

void writeInode(inode nd){
    int n = nd.id;
    int numPerBlock = BLOCK_SIZE / sizeof(inode);
    int block = 3 + (n / numPerBlock);
    char buf[BLOCK_SIZE];

    block_read(block, (void*) buf);



    inode* rp = (inode*) (buf + (n % numPerBlock) * sizeof(inode));
    //*rp = nd;
    memcpy(rp, &nd, sizeof(inode));

    block_write(block, (const void*) buf);


}


//checks whether the path to a file is legit by comaring it to iNodes.
//if an iNode exists for the file, returns that inode*
inode checkiNodePathName(const char *path){
    int i;
    for(i = 0; i < INODE_LIST_SIZE; i++){

      	if(isInodeFree(i) == 0){
            log_msg("got hereee, i is %d\n",i);
            inode in = readInode(i);
            log_msg("path = %s, node's path = %s\n",path, in.path);

      	    if(strcmp(path, in.path) == 0){
      		      return in;
      	    }
      	}

    }

    inode badresult;
    badresult.id = -1;

    return badresult;
}

void fillStatBuff(struct stat *statbuf, inode iNode){
    statbuf->st_mode =  iNode.mode;//S_IFREG | 0777;
    statbuf->st_nlink = iNode.hardlinks;
    statbuf->st_size = iNode.size;
    statbuf->st_blocks = iNode.size / BLOCK_SIZE + 1;

    statbuf->st_ino = iNode.id;

    statbuf->st_ctime = time(0);
    statbuf->st_mtime = time(0);
    statbuf->st_atime = time(0);
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

int findNextOpenBlock(){
    int i;
    for (i = 0; i < BLOCK_LIST_SIZE; i++) {
        if(isBlockFree(i) == 1){
            return i;
        }
    }

    return -1;
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


    char bmbuf[BLOCK_SIZE];
    for (i = 0; i < BMAP_SIZE; i++) {
        bmbuf[i] = 0xFF;
    }

    block_write(1, (const void*) bmbuf);





    //set up root inode
    setInode(0,0);
    inode root;
    root.id = 0;
    memcpy(root.path, "/", 2);
    root.mode = S_IFDIR | 0766; //S_IROTH | S_IWOTH | S_IXOTH | S_IXGRP | S_IWGRP | S_IRGRP | S_IXUSR | S_IWUSR | S_IRUSR;
    root.size = 0;
    root.hardlinks = 2;
    writeInode(root);

    /*
    setInode(1,0);
    inode dot;
    dot.id = 1;
    memcpy(dot.path, ".", 2);
    dot.mode = S_IFDIR | 0777;
    dot.size = 0;
    dot.hardlinks = 2;
    writeInode(dot);

    setInode(2,0);
    inode dotdot;
    dotdot.id = 2;
    memcpy(dotdot.path, "..", 3);
    dotdot.mode = S_IFDIR | 0777;
    dotdot.size = 0;
    dotdot.hardlinks = 2;
    writeInode(dotdot);
    */

    inode test = readInode(0);
    log_msg("test has mode %d links %d and id %d",test.mode,test.hardlinks,test.id);

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

	     statbuf->st_mode = S_IFDIR | 0777;
	     statbuf->st_nlink = 2;

       //fillStatBuff(statbuf, readInode(0));
       log_stat(statbuf);

       return retstat;
    }
    log_msg("didnt fail yet\n");
    inode n = checkiNodePathName(path);
    log_msg("still didnt fail\n");
    if(n.id != -1){
       log_msg("found the inode\n");
	     fillStatBuff(statbuf, n);
       log_stat(statbuf);
       return retstat;
    }
    log_msg("didnt found the inode\n");

    //*statbuf = (struct stat) {0};
    log_stat(statbuf);
    return -ENOENT;
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
            in.hardlinks = 1;

            memcpy(in.path, path, 256);

            in.data[0] = findNextOpenBlock();

            if(in.data[0] == -1){
                return -1;
            }

            setBlock(in.data[0], 0);

            writeInode(in);
            fi->fh = in.id;//why does this not throw numerical out of range like open???

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
    if(file.id == -1){
	      return -1;
    }

    //remove a hardlink from the specified inode
    file.hardlinks -= 2;
    if(file.hardlinks < 1){
      	int i;
      	//set all the blocks that the file uses to free, so other files can use the space if needed
      	for(i = 0; i <= file.size / BLOCK_SIZE + 1; i++){
      	    setBlock(file.data[i], 1);
        }
    }

    setInode(file.id, 1);

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
    int i;
    log_msg("\nsfs_open(path\"%s\", fi=0x%08x)\n",
	    path, fi);
    int flags = fcntl(fi->fh, F_GETFL);
    if(flags == -1){
      return -1;
    }
    for(i = 0; i < INODE_LIST_SIZE; i++){
      if(isInodeFree(i) == 0){
          inode in = readInode(i);
          if(strcmp(path, in.path) == 0){
            	fi->fh = i; //this doesnt work even though it works in create wtf
				            //throws a fucking numerical result out of range but it doesnt for create??
		          return 0;
          }
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


    inode file = checkiNodePathName(path);
    if(file.id == -1){
      	//couldn't find the file
      	return -1;
    }


    fi->fh = -1;
    //fi->flags = fi->flags ^ fi->flags;

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

    inode file = checkiNodePathName(path);
    if(file.id == -1){
      	// file doesn't exist to read from
      	return -EBADF;
    }

    if(offset > file.size){
      	//cant start reading after the EOF, and cant read more than the file has
      	//for the second, we can actually read what the file has, but we can't read more than that. should we just read all the stuff?
      	return -EFAULT;
    }
    if(size == 0){
	     return 0;
    }

   int bytesWritten = 0;

   memset(buf, 0, size);

   int blk = offset / BLOCK_SIZE;
   int chr = offset % BLOCK_SIZE;
   int i;
   char diskbuf[BLOCK_SIZE];
   block_read(D_BLOCK_START + file.data[blk], (void*) diskbuf);


   for (i = 0; i < size; i++) {
      buf[i] = diskbuf[chr];
      chr++;
      bytesWritten++;
      if(chr % BLOCK_SIZE == 0){
          blk++;
          block_read(D_BLOCK_START + file.data[blk], (void*) diskbuf);


	        chr = 0;
      }
   }

   return bytesWritten;
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

    inode file = checkiNodePathName(path);
    if(file.id == -1){
      	// file doesn't exist to read from
      	return -EBADF;
    }

    if(fi->fh == -1){
        return -EBADF;
    }

    if(size == 0){
	     return 0;
    }

   int bytesWritten = 0;
   log_msg("\ngiven path= %s, inode path= %s \n ", path, file.path);
   int blk = offset / BLOCK_SIZE;
   int chr = offset % BLOCK_SIZE;
   int i;

   char diskbuf[BLOCK_SIZE];
   //char fromFile[BLOCK_SIZE];

   int blksOwned = file.size / BLOCK_SIZE + 1;

   while (blk > blksOwned) {
       log_msg("got here\n");
       int nextBlk = findNextOpenBlock();
       if(nextBlk == -1){
          return -1;
       }
       file.data[blksOwned + 1] = nextBlk;
       setBlock(nextBlk, 0);
       blksOwned++;
   }

   block_read(D_BLOCK_START + file.data[blk], (void *) diskbuf);


   //memcpy(diskbuf, fromFile, BLOCK_SIZE);
   //memset(diskbuf, 0, BLOCK_SIZE);

   for (i = 0; i < size; i++) {
      diskbuf[chr] = buf[i];
      chr++;
      bytesWritten++;

      if(chr + (blk * BLOCK_SIZE) > file.size){
          file.size++;
          if(file.size % BLOCK_SIZE == 0){
              if(file.size == 16777216){
                  block_write(D_BLOCK_START + file.data[blk], (const void*) diskbuf);
                  writeInode(file);
                  return bytesWritten;
              }
              int nbl = findNextOpenBlock();
              if(nbl == -1){
                  return -1;
              }
              file.data[blk + 1] = nbl;
              setBlock(nbl, 0);
          }
      }

      if(chr % BLOCK_SIZE == 0){
          block_write(D_BLOCK_START + file.data[blk], (const void*) diskbuf);


          blk++;
          block_read(D_BLOCK_START + file.data[blk], (void*) diskbuf);


          chr = 0;
      }

   }

   block_write(D_BLOCK_START + file.data[blk], (const void*) diskbuf);


   writeInode(file);

   return bytesWritten;
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
    int i;
    for(i = 1; i < INODE_LIST_SIZE; i++){
      //log_msg("got here 1\n");
      if(isInodeFree(i) == 0){
        //log_msg("got here 2\n");
        inode in = readInode(i);
        memcpy(filename, in.path + 1, 255);
        //log_msg("got here 3\n");

        int x = filler(buf, filename, NULL, 0);
        if(x != 0){
            log_msg("\nIT FAILED\n");
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

  //.rmdir = sfs_rmdir,
  //.mkdir = sfs_mkdir,

  //.opendir = sfs_opendir,
  .readdir = sfs_readdir,
  //.releasedir = sfs_releasedir
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
