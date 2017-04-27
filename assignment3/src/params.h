/*
  Copyright (C) 2012 Joseph J. Pfeiffer, Jr., Ph.D. <pfeiffer@cs.nmsu.edu>

  This program can be distributed under the terms of the GNU GPLv3.
  See the file COPYING.

  There are a couple of symbols that need to be #defined before
  #including all the headers.
*/

#ifndef _PARAMS_H_
#define _PARAMS_H_

// The FUSE API has been changed a number of times.  So, our code
// needs to define the version of the API that we assume.  As of this
// writing, the most current API version is 26
#define FUSE_USE_VERSION 26

// need this to get pwrite().  I have to use setvbuf() instead of
// setlinebuf() later in consequence.
#define _XOPEN_SOURCE 500

// maintain bbfs state in here
#include <limits.h>
#include <stdio.h>
#include <sys/stat.h>


typedef struct inode_{
    short id;
    char* path;

    short open;

    mode_t mode;

    //size in bytes of the file so far. size in blocks can be calculated using BLOCK_SIZE
    int size;

    //32768 blocks can hold 16MB, enough to hold the memallocator's file
    int* data;

    //number of hard links to the file
    int hardlinks;
} inode;

struct sfs_state {
    FILE *logfile;
    char *diskfile;
};

#define SFS_DATA ((struct sfs_state *) fuse_get_context()->private_data)

#endif
