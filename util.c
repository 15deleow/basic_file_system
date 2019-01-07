#include "head.h"
// globals defined in main.c

extern MINODE minode[NMINODE];
extern MINODE *root;
extern PROC   proc[NPROC], *running;
extern char gpath[128];
extern char *name[64];
extern int n;
extern int fd, dev, debug = 0, iblk;
extern int nblocks, ninodes, bmap, imap, inode_start;
extern char line[256];
extern char *cmd; // will contain command string
extern char *path[32]; // will contain pathname string and/or file mode

// for use with ls
char *t1 = "xwrxwrxwr-------";
char *t2 = "----------------";

// will break up pathname into component strings
// and into a global variable char * array
int tokenize(char *pathname)
{
    int i;
    char *temp;
    //char copy[128];
    n = 0;
    strcpy(gpath, pathname);
    temp = strtok(gpath, "/");
    while (temp)
    {
        name[n] = temp;
        temp = strtok(0, "/");
        n++;
    }
}

// read diskimage by blocks using file descriptor
// read block into a buffer area in memory
int get_block(int fd, int blk, char buf[])
{
    lseek(fd, (long)blk * BLKSIZE, 0);
    read(fd, buf, BLKSIZE);
}

// write to diskimage by blocks using file descriptor
// from a buffer a buffer area in memory
int put_block(int fd, int blk, char buf[])
{
    lseek(fd, (long)blk * BLKSIZE, 0);
    write(fd, buf, BLKSIZE);
}

// Will return a pointer to the in-memory minode containing
// the INODE of (dev,ino) 
// The returned minode is unique and only one in memory
MINODE *iget(int dev, int ino)
{
    char buf[BLKSIZE];
    int blk, disp;
    INODE *ip;

    // search in-memory minodes first
    for (int i = 0; i < NMINODE; i++)
    {
        if (minode[i].ino == ino && minode[i].dev == dev && minode[i].refCount)
        { //Found!
            minode[i].refCount++;
            return &minode[i];
        }
    }
    //Did not find matchning inode
    for (int i = 0; i < NMINODE; i++)
    {
        // search for an empty in-memory INODE
        // first one found, make in it an minode that is needed
        if (minode[i].refCount == 0)
        {
            minode[i].refCount = 1;
            minode[i].dev = dev;
            minode[i].ino = ino;
            minode[i].dirty = 0;
            minode[i].mounted = 0;

            blk = (ino - 1) / 8 + iblk;
            disp = (ino - 1) % 8;

            get_block(dev, blk, buf);
            ip = (INODE *)buf + disp;
            minode[i].INODE = *ip;
            return &minode[i];
        }
    }
    printf("Insufficient Inodes!\n");
    return 0;
}

// Will release a used minode pointed to by a MINODE ptr
// Will decrement refCount by 1. 
// if it is non-zero, minode is being used by other users, return
// else the INODE is written back to disk by put_block()
int iput(MINODE *mip)
{
    int i, blk, disp;
    char buf[BLKSIZE];
    INODE *ip;

    if (mip == 0)
        return 0;
    mip->refCount--;

    if (mip->refCount > 0)
        return 0; //Other is using this inode
    if (!mip->dirty)
        return 0; //Nothing has changed
    //Need to Write Inode back to disk
    if (debug)
        printf("iput: dev=%d ino=%d\n", mip->dev, mip->ino);

    blk = (mip->ino - 1) / 8 + iblk; //block
    disp = (mip->ino - 1) % 8;       //offset

    get_block(mip->dev, blk, buf);

    ip = (INODE *)buf + disp;
    *ip = mip->INODE; //copy INODE to *ip

    put_block(mip->dev, blk, buf);
    mip->refCount = 0;
}

// it implements the file system tree traversal algorithm
// to search for INODE number (ino) of a specified pathname
// will return that INODE number
int getino(int dev, char *pathname)
{
    int i, ino, blk, disp;
    char buf[BLKSIZE];
    INODE *ip;
    MINODE *mip;
    dev = root->dev; // only ONE device so far

    if (debug)
        printf("getino: pathname=%s\n", pathname);

    if (strcmp(pathname, "/") == 0)
        return 2;

    if (pathname[0] == '/')
        mip = iget(dev, 2);
    else
        mip = iget(running->cwd->dev, running->cwd->ino);

    strcpy(buf, pathname);
    tokenize(buf); // n = number of token strings

    for (i = 0; i < n; i++)
    {
        if (debug)
        {
            printf("===========================================\n");
            printf("getino: i=%d name[%d]=%s\n", i, i, name[i]);
        }

       
        // serach for file given my name[i]
        // return its INODE number if found
        // or return 0 if not found
        ino = search(mip, name[i]);

        
        if (ino == 0)
        {
            iput(mip);
            printf("name %s does not exist\n", name[i]);
            return 0;
        }

        iput(mip); // deallocate MINODE ptr
    
        // return MINODE ptr determined by (dev,ino)
        mip = iget(dev, ino);
    }
    return ino;
}

// Will search for token strings in successive directories
// will search directory for name. Will return INODE number
// else it will return 0, not found
int search(MINODE *mip, char *name)
{
    int i;
    char *cp, c;
    char dbuf[1024];
    DIR *dp;
    INODE *ip;

    if (debug)
        printf("Search for %s in MINODE = [%d, %d]\n", name, mip->dev, mip->ino);

    ip = &(mip->INODE);

    for (i = 0; i < 12; i++)
    { // assume: DIRs have at most 12 direct blocks
        if (debug)
            printf("i_block[%d] = %d\n", i, ip->i_block[i]);
        if (ip->i_block[i] == 0)
            return 0;
        get_block(dev, ip->i_block[i], dbuf);

        cp = dbuf;
        dp = (DIR *)dbuf;

        while (cp < dbuf + 1024)
        {
            c = dp->name[dp->name_len];
            dp->name[dp->name_len] = 0;

            if (debug)
                printf("%4d %4d %4d   %s\n", dp->inode, dp->rec_len, dp->name_len, dp->name);
            if (strcmp(dp->name, name) == 0)
            {
                if (debug)
                {
                    printf("Found %s with ino %d\n", dp->name, dp->inode);
                    printf("group=%d inumber=%d\n", ip->i_gid, dp->inode - 1);
                    printf("blk = %d disp = %d\n", ip->i_block[i], 1);
                }
                return (dp->inode);
            }
            dp->name[dp->name_len] = c;
            cp += dp->rec_len;
            dp = (DIR *)cp;
        }
    }
    return 0;
}

// will return the name string of a dir-entry identified by myino in the parent directory
int findmyname(MINODE *parent, u32 myino, char *myname)
{
    // search for myino in parent INODE;
    // copy its name string into myname[ ]

    int i;
    char *cp, c, sbuf[BLKSIZE];
    DIR *dp;
    INODE *ip;

    ip = &(parent->INODE);

    /**********  search for a file name ***************/
    for (i = 0; i < 12; i++)
    { 
        /* search direct blocks only */
        if (debug)
            printf("search: i=%d  i_block[%d]=%d\n", i, i, ip->i_block[i]);

        if (ip->i_block[i] == 0)
            return 0;

        // open i_block to get dir_entries
        get_block(dev, ip->i_block[i], sbuf);
        dp = (DIR *)sbuf;
        cp = sbuf;

        // traverse the i_block entries
        while (cp < sbuf + BLKSIZE)
        {
            c = dp->name[dp->name_len];
            dp->name[dp->name_len] = 0;

            // if the 
            if (myino == dp->inode)
            {
                strcpy(myname, dp->name);
                return 1;
            }
            dp->name[dp->name_len] = c;
            cp += dp->rec_len;
            dp = (DIR *)cp;
        }
    }
    return 0;
}

// Will return the inode number of . the parent directory
int findino(MINODE *mip, u32 *myino)
{
    int i, pino;
    char *cp, c, sbuf[BLKSIZE];
    DIR *dp;
    INODE *ip;

    ip = &(mip->INODE);
    *myino = mip->ino;

   /**********  search for a file name ***************/
    for (i = 0; i < 12; i++)
    { 
        /* search direct blocks only */
        if (debug)
            printf("search: i=%d  i_block[%d]=%d\n", i, i, ip->i_block[i]);
        
        if (ip->i_block[i] == 0)
            return 0;

        // write blokc data to sbuf
        get_block(dev, ip->i_block[i], sbuf);
        dp = (DIR *)sbuf;
        cp = sbuf;

        // Traverse block dir_entries
        while (cp < sbuf + BLKSIZE)
        {
            c = dp->name[dp->name_len];
            dp->name[dp->name_len] = 0;

            // if name of entry is not ..
            // return its INODE number
            if (!strcmp(dp->name, ".."))
            {
                return dp->inode;
            }

            // else keep traversing the block
            dp->name[dp->name_len] = c;
            cp += dp->rec_len;
            dp = (DIR *)cp;
        }
    }
    return 0;
}


// check if bit is 0 or 1 and will return that value
int tst_bit(char *buf, int bit)
{
    int i, j;
    i = bit / 8;
    j = bit % 8;
    if (buf[i] & (1 << j))
        return 1;
    return 0;
}

// will set a bit to 1
int set_bit(char *buf, int bit)
{
    int i, j;
    i = bit / 8;
    j = bit % 8;
    buf[i] |= (1 << j);
}

// Will reset a bit back to zero
int clr_bit(char * buf, int bit)
{
    int i, j;
    i = bit / 8;
    j = bit % 8;
    buf[i] &= ~(1 << j);
}

// will decrease the number of free INODEs in the INODE block
// in the specified de
int decFreeInodes(int dev)
{
	char buf[1024];
	char buf2[1024];

	get_block(dev, 1, buf);
	sp = (SUPER *)buf;
	sp->s_free_inodes_count--;
	put_block(dev, 1, buf);

	get_block(dev, 2, buf2);
	gp = (GD *)buf2;
	gp->bg_free_inodes_count--;
	put_block(dev, 2, buf2);
}

// Will decrease the number of Free Blocks
int decFreeBlocks(int dev)
{
	char buf[1024];
	char buf2[1024];

    // get superblock
	get_block(dev, 1, buf);
	sp = (SUPER *)buf;

    // decrease free block count
	sp->s_free_blocks_count--;
	put_block(dev, 1, buf);

    // get group block
	get_block(dev, 2, buf2);
	gp = (GD *)buf2;

    // decrease free block count
	gp->bg_free_blocks_count--;
	put_block(dev, 2, buf2);
}

// Will increase the count of free INODEs
// used when a INODE is deallocated
int incFreeInodes(int dev)
{
	char buf[1024];
	char buf2[1024];

	get_block(dev, 1, buf);
	sp = (SUPER *)buf;
	sp->s_free_inodes_count++;
	put_block(dev, 1, buf);

	get_block(dev, 2, buf2);
	gp = (GD *)buf2;
	gp->bg_free_inodes_count++;
	put_block(dev, 2, buf2);
}

// Will increase the count of free Blocks
// used when a block is deallocated
int incFreeBlocks(int dev)
{
	char buf[1024];
	char buf2[1024];
	get_block(dev, 1, buf);
	sp = (SUPER *)buf;
	sp->s_free_blocks_count++;
	put_block(dev, 1, buf);

	get_block(dev, 2, buf2);
	gp = (GD *)buf2;
	gp->bg_free_blocks_count++;
	put_block(dev, 2, buf2);
}


// allocate an inode
int ialloc(int dev)
{
	int numinodes;
	char buf[BLKSIZE];
	SUPER *temp;

	get_block(dev, imap, buf);

	for (int i = 0; i < ninodes; i++)
	{
		if (tst_bit(buf, i) == 0)
		{
			set_bit(buf, i);
			decFreeInodes(dev);
			put_block(dev, imap, buf); // write imap block back to disk
			return i + 1;
		}
	}
	return 0;
}

// Will allocate block and decrease free blocks
int balloc(int dev)
{
    int i;
    char buf[BLKSIZE];

    // read block_bitmap block
    //printf("dev = %d\n", dev);

    get_block(dev, bmap, buf);

    for (i = 0; i < nblocks; i++)
    {
        if (tst_bit(buf, i) == 0)
        {
            set_bit(buf, i);
            decFreeBlocks(dev);

            put_block(dev, bmap, buf);

            return i + 1;
        }
    }
    printf("balloc(): no more free inodes\n");
    return 0;
}

// Will deallocate INODE
int idealloc(int dev, int mipino)
{
	char buf[BLKSIZE];
	get_block(dev, imap, buf);
	clr_bit(buf, mipino - 1); // set to zero
	put_block(dev, imap, buf);
	incFreeInodes(dev);
}

// Will de-allocate a block
int bdealloc(int dev, int iblock)
{
	char buf[BLKSIZE];
	get_block(dev, bmap, buf);

	clr_bit(buf, iblock - 1);

	put_block(dev, bmap, buf);
	incFreeBlocks(dev);
}

// Will add entry to parent directory block
// at the end of the last entry 
// will allocate another block if current i_block
// has no more space
int enter_name(MINODE *pip, int ino, char *name)
{
    int i;
    char buf[BLKSIZE];
    char *cp;
    DIR *dp;

    // Traverse the 12 direct blocks
    for (i = 0; i < 12; i++)
    {
        // if block is zero, dir is empty
        if (pip->INODE.i_block[i] == 0)
            break;

        // open parent dir entry
        get_block(dev, pip->INODE.i_block[i], buf);
        cp = buf;
        dp = (DIR *)buf;

        // traverse dir until last entry
        while (cp + dp->rec_len < buf + 1024)
        {
            cp += dp->rec_len;
            dp = (DIR *)cp;
        }

        int need_length = 4 * ((8 + strlen(name) + 3) / 4); // needed block size for file

        int last_entry_ideal_len = 4 * ((8 + dp->name_len + 3) / 4); // size of last entry

        int remain = dp->rec_len - last_entry_ideal_len; //last entry's rec_len - ideal-length

        // if size of block remaining > the length of needed data size
        // no block allocations is needed, add to entry to block
        if (remain >= need_length)
        {
            // fill dir_entry data of new entry
            dp->rec_len = last_entry_ideal_len;
            cp += dp->rec_len;
            dp = (DIR *)cp;
            dp->rec_len = remain;
            dp->inode = ino;
            dp->name_len = strlen(name);

            strncpy(dp->name, name, dp->name_len);

            // write back to memory
            put_block(dev, pip->INODE.i_block[i], buf);
            return 1;
        }
    }

    // No space in existing datablock
    int bno = balloc(dev); // allocate a block

    // if block is allocated correctly
    // get block into buf
    // fill up its first entry with file data
    if (bno)
    {
        pip->INODE.i_size += BLKSIZE;
        pip->INODE.i_block[i] = bno;
        get_block(dev, bno, buf);
        cp = buf;
        dp = (DIR *)buf;
        dp->name_len = strlen(name);
        dp->rec_len = BLKSIZE;
        dp->inode = ino;
        strncpy(dp->name, name, dp->name_len);
        put_block(dev, pip->INODE.i_block[i], buf);
        pip->dirty = 1;
        iput(pip);
    }
    else
        printf("Could not allocate new block\n");

    return 0;
}


// will print out file/dir information given by MINODE and name
int ls_file(MINODE *mip, char *name)
{
    int k;
    u16 mode, mask;
    char mydate[32], *s, *cp, ss[32];
    char buf[BLKSIZE];

    mode = mip->INODE.i_mode;

    if ((mode & 0xF000) == 0x8000)
        printf("%c", '-');
    if ((mode & 0xF000) == 0x4000)
        printf("%c", 'd');
    if ((mode & 0xF000) == 0xA000)
        printf("%c", 'l');

    for (int i = 8; i >= 0; i--)
    {
        if (mode & (1 << i))
            printf("%c", t1[i]);
        else
            printf("%c", t2[i]);
    }

    printf("%4d", mip->INODE.i_links_count);
    // print user id and group id. Will be 0 if at root
    printf("%4d", mip->INODE.i_uid);
    printf("%4d", mip->INODE.i_gid);
    printf("  ");

    // print size of file
    printf("%8ld  ", mip->INODE.i_size);

    // print creation time
    s = mydate;
    s = (char *)ctime(&mip->INODE.i_atime);
    s = s + 4;

    //copy contents of s into ss upto 12 chars
    strncpy(ss, s, 12);

    ss[12] = 0; // set last index in ss to null

    printf("%s", ss);

    // print name of file
    printf("    %s", name);

    // if file is a symbolic link, print name of file it points to ->
    if (S_ISLNK(mode))
    {   get_block(dev,mip->INODE.i_block[0],buf);
        printf(" -> %s", buf);
    }
    printf("\n");
}

// will search each path in pathname to get needed directory
int ls_dir(MINODE *mip)
{
    int i;
    char sbuf[BLKSIZE], temp[256];
    DIR *dp;
    char *cp;
    MINODE *dip;

    // will search root block for each path in pathname
    // and print its information like ls -l
    for (i = 0; i < 12; i++)
    {
         /* search direct blocks only */
        if (debug)
            printf("i_block[%d] = %d\n", i, mip->INODE.i_block[i]);

        if (mip->INODE.i_block[i] == 0) // if the block is 0, there are no more blocks, leave function
            return 0;

        // get data block into sbuf
        get_block(mip->dev, mip->INODE.i_block[i], sbuf);

        dp = (DIR *)sbuf; // cast sbuf as a dir entry
        cp = sbuf; // cp will point to sbuf

        while (cp < sbuf + BLKSIZE) // while cp is not at end of data block
        {
            // get inode of entry and entry name and call ls_file
            // will print out file information like ls -l
            strncpy(temp, dp->name, dp->name_len);
            temp[dp->name_len] = 0;
            // get MINODE of dir entry
            dip = iget(dev, dp->inode);

            // call this to print out file data of dir entry 
            ls_file(dip, temp);
            iput(dip);

            cp += dp->rec_len;
            dp = (DIR *)cp;
        }
    }
}


// will print out content of stat structure, containing info
// of a file
int print_stat(struct stat fstat, char * filename)
{
    char *s, ss[64];
    printf("\n-------------- STAT-------------------\n");
    printf("File: %s\n", filename);
    printf("Size: %d     ", fstat.st_size);
    printf("Blocks: %d   ", fstat.st_blocks);

    // decide what type the file is and print it to screen
    if((fstat.st_mode & DIR_MODE) == DIR_MODE)
        printf("directory\n");
    
    else   
    {
        // if a file, decide if empty or non-empty
        if(fstat.st_size == 0)
            printf("regular empty file\n");
        else
            printf("regular file\n");
    }

    printf("Device: %d   ", fstat.st_dev);
    printf("Inode: %d    ", fstat.st_ino);
    printf("Links: %d    \n", fstat.st_nlink );
    
    
    /* Will print out the access time, modify time, and 
       the change time of the file 
    */
    
    s = (char *)ctime(&fstat.st_atime);
    s = s + 4;
    //copy contents of s into ss upto 12 chars
    strncpy(ss, s, 12);
    ss[12] = 0; // set last index in ss to null
    printf("Accessed: %s\n", ss);


    s = (char *)ctime(&fstat.st_mtime);
    s = s + 4;
    //copy contents of s into ss upto 12 chars
    strncpy(ss, s, 12);
    ss[12] = 0; // set last index in ss to null
    printf("Modified: %s\n", ss);

    s = (char *)ctime(&fstat.st_ctime);
    s = s + 4;
    //copy contents of s into ss upto 12 chars
    strncpy(ss, s, 12);
    ss[12] = 0; // set last index in ss to null
    printf("Changed: %s\n", ss);
    printf("---------------------------------------\n");
}


// Will allocate an INODE
// setup INODE data fields with information of new file
// will clear up all blocks in INODE block
// Will then enter file into parent directory block
int mycreat(MINODE *pip, char *name)
{
	int ino, bno;
	char *cp;
    MINODE *mip;
    INODE *ip;

	//allocate an inode for new file
	ino = ialloc(dev);

    // if ino is 0, failed to allocate INODE
    if(!ino)
    {
        printf("Could not allocate new inode\n");
        return -1;
    }
	
	//load the inode into a minode[] (in order to write contents to the INODE in memory)
	mip = iget(pip->dev, ino);
	ip = &mip->INODE;

	// setup INODE data structure for file
	ip->i_mode = FILE_MODE;		                      // FILE type and permissions
	ip->i_uid = running->uid;	                     // Owner uid
	ip->i_gid = running->gid;	                    // Group Id
	ip->i_size = 0;		                  		   // Size in bytes
	ip->i_links_count = 1;	                      // Links count= 1 
	ip->i_atime = time(0L); 					 // updata access time to current
	ip->i_ctime = time(0L); 					// update creation time to current
	ip->i_mtime = time(0L); 				   // updata modified time to current
	ip->i_blocks = 0;                	      // LINUX: Blocks count in 512-byte chunks
	mip->dirty = 1;                          // mark minode dirty

    //clear up all  blocks for file
	for (int i = 0; i< 15; i++)                    
	{
		ip->i_block[i] = 0;
	}

	iput(mip);                                // deallocate MINODE *ptr

	//finally enter name ENTRY into parent's directory
	enter_name(pip, ino, name);
	return ino;
}


// Will deallocate a INODEs data blocks
int my_truncate(INODE *ip)
{
    int i = 0; 
    int size = ip->i_size;
    int blocks = ip->i_size /BLKSIZE +1; // number of blocks
    int remaining = blocks; // remaining blocks
    char dbuf[BLKSIZE];
    char *cp;
    INODE *lip;

    //deallocate each i_block in INODE
    for(i = 0; ip->i_block[i] != 0 && i < 12; i++)
    {
        bdealloc(dev, ip->i_block[i]);
        remaining--;
    }

    if(remaining > 0)
    {
        get_block(dev, ip->i_block[12], dbuf);
        cp = dbuf;
        dp = (DIR *)dbuf;

        // while cp has not reached end of buf
        // and remaining blocks > 0
        // and dp-> inode != 0
        while( cp < dbuf + BLKSIZE && remaining && dp->inode) 
        {
            lip = iget(root->dev, dp->inode);

            // deallocated indirect blocks in INODE
            for(i = 0; lip->i_block[i] != 0 && i < 12; i++)
            {
                bdealloc(root->dev, lip->i_block[i]);
            }

            cp += 4; // increment cp by 4, next entry in sbuf
            dp =(DIR *)cp; // get content of cp
            remaining--; // decrease block count;
        }
    }

    return 0;
}


/* Will add name to i_block of new file and make size to size of old file 
   if old file name execeeds 84 char, cutoff at 84 char because max size is 84.
   write block back to memory
*/
int enter_name_link_file(MINODE *ip, char *name)
{
    char buf[BLKSIZE];
  

    // get INODE block information
    get_block(dev, ip->INODE.i_block[0], buf);

    memset(buf,'\0', BLKSIZE);

    // add old filename to newfile
    if (strlen(name) < 84) 
    {
        ip->INODE.i_size = strlen(name); // set size to size of old file
        
        strncpy(buf, name, strlen(name)); // copy old file name into buf

        buf[strlen(name)] = '\0';         // add null in buff where old name ends

        put_block(dev, ip->INODE.i_block[0], buf); // write block back to memory
        return 1;
    }
    else // similar as above but will cutoff name at 84 characters, because max is 84 bytes
    {
        ip->INODE.i_size = strlen(name);
        strncpy(buf, name, 83); //up to 84 bytes
        buf[83]='\0';
        put_block(dev, ip->INODE.i_block[0], buf);
        return 1;
    }
}

/* Will get name of file that is links to and print it */
int my_readlink()
{
    MINODE *mip;
    int ino, mode;
    char buf[BLKSIZE], *basecopy,*filename;

    basecopy = strdup(path[0]);
    filename = basename(basecopy);

    ino = getino(dev, path[0]);
    if (!ino)
        return -1;
    mip = iget(dev, ino);
    mode = mip->INODE.i_mode;

    //Check it is a reg or lnk file
    if (!S_ISLNK(mode))
    {
        printf("%s is not a link file\n", filename);
        return -1;
    }

    get_block(dev, mip->INODE.i_block[0], buf);
    printf("%s\n",buf);
}