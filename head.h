// Libraries
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <ext2fs/ext2_fs.h>
#include <string.h>
#include <libgen.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

// File system setup
typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

typedef struct ext2_super_block SUPER;
typedef struct ext2_group_desc  GD;
typedef struct ext2_inode       INODE;
typedef struct ext2_dir_entry_2 DIR;

SUPER *sp;
GD    *gp;
INODE *ip;
DIR   *dp;   

#define BLKSIZE  1024

// file system table sizes
#define NMINODE    100
#define NOFT       40
#define NFD        10
#define NMTABLE    10
#define NMOUNT      4
#define NPROC       2

// BLcok number of EXT2 FS on FD
#define SUPERBLOCK 1
#define GDBLOCK 2
#define ROOT_INODE 2

// Default dir and regular file modes
#define DIR_MODE 0x41ED
#define FILE_MODE 0x81A4
#define SYMLINK   0xA1A4
#define SUPER_MAGIC 0xEF53
#define SUPER_USER 0

// PROC status
#define FREE 0
#define BUSY 1

// testing
#define DEBUG 1
#define RUN 0


// In-Memory inodes structure
typedef struct minode
{
  INODE INODE; // disk inode
  int dev, ino;
  int refCount; // use count
  int dirty; // mounted flag
  int mounted; // mount table pointer
  struct mntable *mptr; // ignored for simple FS
}MINODE;

// Mount Table Struture
typedef struct mntable
{
    int dev; // device number; 0 for free
    int ninodes; // from superblock
    int nblocks; 
    int free_blocks; // from superblock and GD
    int free_inodes;
    int bmap; // from group descriptor
    int imap;
    int iblock; // inodes start block
    MINODE *mntDirPtr; // mount point DIR Pointer
    char devName[64]; // device name
    char mntName[64]; // mount point DIR name
}MTABLE;

// Open File Table
typedef struct oft
{
  int  mode; // mode of opened file
  int  refCount; // number of PROCs sharing this instance
  MINODE *mptr; // pointer to minode of file
  int  offset; // byte offset for R|W
}OFT;

// PROC structure
typedef struct proc
{
  struct proc *next; // next proc pointer
  int          pid; // pid = 0 to NPROC - 1 // process ID
  int          ppid;// Parent process ID
  int          status; 
  int          uid, gid; // uid: user id, group ID
  MINODE      *cwd; // pointer 
  OFT         *fd[NFD]; // pointer to an array of OFTs
}PROC;

