#include "head.h"

// Global variables from main.c
extern MINODE minode[NMINODE];
extern MINODE *root;
extern PROC   proc[NPROC], *running;

extern char gpath[128];
extern char *name[64];


extern int fd, dev, iblk;

extern int nblocks, ninodes, bmap, imap, inode_start;
extern char line[256];

extern char *cmd; // will contain command string
extern char *path[32]; // will contain pathname string and/or file mode
extern int debug; // for debuggin code



// This funtion will be called by my_mkdir and will to the actual
// creation of the new dir after all checks are good.
// will call enter_name() which will write entry into parent dir block
int makeDir(MINODE *pmip, char *baseName)
{
	//allocate inode and disk block
	int ino = ialloc(dev);
	int i;
	char *cp;
	char buf[BLKSIZE];

	MINODE *mip;
	INODE *ip;

	// FAiled to allocate an INODE
	if(!ino)
	{
		printf("Could no allocated INODE\n");
		return -1;

	}

	// allocated a INODE Block
	int bno = balloc(dev);

	// failed to allocated a BLOCK
	if(!bno)
	{
		printf("Could not allocate a block\n");
		return -1;
	}
	
	// get INODE into minode
	mip = iget(dev, ino);
	ip = &mip->INODE;

	/* Setup INODE data fields */
	ip->i_mode = 0x41ED; // DIR type and includes permissions
	ip->i_uid = running->uid; //sets the User ID (Owner ID)
	ip->i_gid = running->gid;//sets the Group ID

	ip->i_size = BLKSIZE; //Set to the size of a directory (1024 in this case)
	ip->i_links_count = 2;// For . and ..

	//sets the access, creation, and modified time to current time 
	ip->i_atime = time(0L);
	ip->i_ctime = time(0L);
	ip->i_mtime = time(0L);

	ip->i_blocks = 2; //number of 512-Byte sectors 

	ip->i_block[0] = bno;
	for (i = 1; i < 15; i++)
	{
		//sets all blocks to 0 
		ip->i_block[i] = 0;
	}

	mip->dirty = 1;
	iput(mip);


	dp = (DIR *)buf;
	cp = buf;


	//makes . entry
	dp->inode = ino;
	dp->rec_len = 12;
	dp->name_len = strlen(".");
	dp->name[0] = '.';

	cp += dp->rec_len;
	dp = (DIR*)cp;

	dp->inode = pmip->ino; // .. points to parent directory
	dp->rec_len = BLKSIZE-12;
	dp->name_len = 2;
	dp->name[0] = '.';
	dp->name[1] = '.';
	
	put_block(dev, bno, buf);

	enter_name(pmip, ino, baseName);

}

/*
	THis function will creat a directory given by pathname
	Will check if dir does not exist in parent dir
	and if parent dir is even a dir
	if both checks are good, will creat . and .. for new dir
	will then place entry into parent directory
*/

int my_mkdir()
{
	int i, ino;
	MINODE *pmip;
	INODE *pip;

	char pathname[64];
	char buf[BLKSIZE], tempDirName[BLKSIZE], tempBaseName[BLKSIZE];
	char dirName[BLKSIZE], baseName[BLKSIZE];

	strcpy(pathname, path[0]);

	//makes copies to so the original isn't destroyed
	strcpy(tempDirName, pathname);
	strcpy(tempBaseName, pathname);

	//gets the dir name and base name of the given path
	strcpy(dirName, dirname(tempDirName));
	strcpy(baseName, basename(tempBaseName));


	// check if pathname starts at root directory or in a subdirectory
	if(pathname[0] == '/')
	{
		dev = root->dev;
	}
	else
	{
		dev = running->cwd->dev;
	}
	

	//get inode of parent directory
	// put it in MINODE
	ino = getino(dev, dirName);
    
    if(!ino)
        return -1;
    
    pmip = iget(dev, ino);


	pip = &pmip->INODE; // INODE *pip now has access to INODE data struct pointed by MINODE *ptr

	// check mode of parent and see if it is a dir type
	if (!S_ISDIR(pip->i_mode))
	{
		printf("Not a valid directory\n");
		return 0;
	}

	// check if child does exist in parent
	// will search each block in parent for specified child
	if (search(pmip, baseName))
	{
		printf("%s already exist\n", pathname);
		return 0;
	}
	
	printf("Making directory %s...\n", baseName);

	// child does not exist, create a block called child
	// setup its data fields
	makeDir(pmip, baseName);

	// increment link count to parent dir
	pip->i_links_count++;
	pip->i_atime = time(0L); // update accessed time
	pmip->dirty = 1;

    printf("Made Directory %s\n", baseName);

	iput(pmip); // release MINODE ptr
	return 0;

}


// Will remove the directory specified by name from the
// parent directory. Will check where in the BLOCK the
// child is located at. 
// if at front without other entries, clear all blcoks
// if at front with entries, overwrite entries by shifting all entires left and clear last entry
// if at end, update prev entry rec-len to span the lenght of the rm entry as well. 
// if at middle will add rec_lec to be remvoed to last entry rec-len and shift over all entries 
// to left to overite where current entry begins.
int myrmchild(MINODE *pip, char *name)
{
    char *cp, buf[BLKSIZE];
    DIR *dp, *prev;
    int current_position;
    // 1. Seach for child name in INODE Block
    for (int i = 0; i < 12; i++)
    {
        current_position = 0;
        // Child dir was not found
        if (pip->INODE.i_block[i] == 0)
        {
            printf("%s does not exist\n", name);
            return -1;
        }

        // get data of entry in parent dir
        get_block(dev, pip->INODE.i_block[i], buf);
        cp = buf;
        dp = (DIR *)buf;
        prev = 0;
        
        // compare current entry to the child to see if they match
        while (cp < buf + BLKSIZE)
        {
            if (strlen(name) == dp->name_len)
            {
                // if names match up, child dir has been found
                if (strncmp(dp->name, name, dp->name_len) == 0)
                { 
                    // FOUND

                    //Compute ideal length of entry to remove
                    int ideal_len = 4 * ((8 + dp->name_len + 3) / 4);

                    // check where the entry is located in INODE block

                    if (ideal_len != dp->rec_len)//must be last entry since rec_len > ideal_len
                    { 
                        // IT IS LAST ENTRY IN BLOCK
                        //update prev entry rec_len to span remaining space in data block
                        prev->rec_len += dp->rec_len;
                    }
                    else if (prev == 0 && cp + dp->rec_len == buf + 1024)//must be first since rec_len = blksize
                    { 
                        // IT IS FIRST AND ONLY ENTRY IN BLOCK
                        char clearbuf[1024];
                        memset(clearbuf, 0, 1024);
                        put_block(dev, pip->INODE.i_block[i], clearbuf);
                        bdealloc(dev, pip->INODE.i_block[i]);//dealloacte entire block
                        pip->INODE.i_size -= BLKSIZE;//decrement by blksize
                        
                        // 
                        for (int j = i; j < 12; j++)
                            if (j == 11)//last will be zero
                                pip->INODE.i_block[11] = 0;
                            else//move over other data blocks since deallocated first block
                                pip->INODE.i_block[j] = pip->INODE.i_block[j + 1];
                    }
                    else//by process of elimination must be in middle
                    { 
                        // IT IS IN MIDDLE OF BLOCK
                        int removed_len = dp->rec_len;//lenght to remove
                        char *temp = buf;
                        DIR *last = (DIR *)temp;

                        while (temp + last->rec_len < buf + BLKSIZE)//find last entry in dir
                        {
                            temp += last->rec_len;
                            last = (DIR *)temp;
                        }

                        last->rec_len += removed_len;//add removed length to last entry
                       
                       //move all entries after current to where current begins
                        //cp should point to beginning of entry to remove, copy over stuff after starting location
                        //of entry to be removed
                        //copy over only the remaining entries after removed entry
                        memcpy(cp, cp + removed_len, BLKSIZE - current_position - removed_len + 1);

                    }
                    // write block into memory
                    put_block(dev, pip->INODE.i_block[i], buf);
                    //pip->INODE.i_links_count--; // decrement links to parent
                    pip->dirty = 1;
                    return 1;
                }
            }
            cp += dp->rec_len;//move to next entry
            current_position += dp->rec_len;//update current position
            prev = dp;//set prev
            dp = (DIR *)cp;
        }
    }
    return 0;
}


/* Will search for the directory that will be deleted 
   will check if it is emtpy, if so, will deallocate
   data block and INODE and then remove entry from
   parent dir. if not empty, cannot remove
*/
int my_rmdir()
{
    int ino, pino;
    MINODE *mip, *pip;
    char *parent, *child, *dircopy, *basecopy, buf[BLKSIZE], *cp;
    DIR *dp;

    dircopy = strdup(path[0]);
    basecopy = strdup(path[0]);
    parent = dirname(dircopy);
    child = basename(basecopy);

	// get INODE number of file
    ino = getino(dev, path[0]);

	// if zero, does not exist
    if(!ino)
        return -1;

	// 
    mip = iget(dev, ino);

    pino = getino(dev, parent);
    
    if(!pino)
        return -1;


    int mode = mip->INODE.i_mode;

    //printf("link_count = %d\n",mip->INODE.i_links_count);

    if (!S_ISDIR(mode))
    {
        printf("%s is not a directory\n", child);
        iput(mip);
        return -1;
    }

     if (mip->INODE.i_links_count == 2)
    { 
        // check if empty. May still have files
        get_block(dev, mip->INODE.i_block[0], buf);

        cp = buf;
        dp = (DIR *)buf;
        cp += dp->rec_len;
        dp = (DIR *)cp;          //get second entry ".."

        if (dp->rec_len != 1012) // dir is non empty
        {
            printf("dir is not empty. cannot remove\n");
            iput(mip);
            return -1;
        }
    }

     if (mip->INODE.i_links_count > 2)
    {
        printf("dir is not empty. cannot remove\n");
        iput(mip);
        return -1;
    }

    // dir can be removed
    printf("Removing %s.... \n", child);

    // deallocate blocks and inode
    for (int i = 0; i < 12; i++)
    {
        if (mip->INODE.i_block[i] == 0)
            continue;
        bdealloc(mip->dev, mip->INODE.i_block[i]);
    }

    idealloc(mip->dev, mip->ino);
    iput(mip);

    pip = iget(dev, pino);

    myrmchild(pip, child);

    pip->INODE.i_links_count--;
    pip->INODE.i_atime = pip->INODE.i_mtime = time(0L);
    pip->dirty = 1;


    printf("Removed %s\n", child);

    iput(pip);

    return 1;
}


