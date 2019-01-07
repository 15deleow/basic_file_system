#include "head.h"
// Global Variables
MINODE minode[NMINODE]; // in-memory INODEs
MINODE *root; // pointer to a root INODE in memory
PROC proc[NPROC], *running; // PROC scructure, current executing PROC
MTABLE mtable[NMTABLE], *mountPtr; // mount tables, pointer to a mount table
OFT oft[NOFT]; // Opened File instance

int dev;
int fd;
int n;

// same as in mount table
int nblocks; // from superblock
int ninodes; // from superblock
int bmap;    // bmap block
int imap;    // imap block
int iblk;    // inodes begin block
int fblocks; // Free Blocks
int finodes; // free INODES

int debug;

char *gpath[256];
char *name[64]; // assume at most 64 components in pathnames

char *cmd; // will contain command string
char *path[32]; // will contain pathname string and/or file mode


// command table containing command strings, which ends with a NULL ptr
char * cmds[] = {"mkdir", "rmdir", "ls", "cd", "pwd", "creat", "link",
                 "unlink", "symlink", "stat", "chmod", "touch","readlink",
                 "menu","pfd","open","close","read","write","lseek",
                "cat","cp","mv", "quit", NULL};
           
char *disk;

// from an array of pointers, get
int get_cmd_index(char * command)
{
    int i = 0;
    while (cmds[i])
    {
        // compare command with cmds[i]
        // if the strings match, cmd was found
        if(strcmp(cmds[i], command) == 0)
            return i;
        i++;
    }
    return -1; // cmd not found
}

// get cmd input, pathnames or file mode
int parseline(char * line)
{
    int n = 0;
    char temp[64];
    char * token;

    strcpy(temp, line);

    token = strtok(temp, " \t");
    cmd = token;

    while(token)
    {
        token = strtok(NULL, " \t");
        path[n] = token;
        n++;
    }
    return 0;
}

/* Initializes all the global data structures */
int fs_init()
{
    MINODE *mip;
    PROC *p;
    char buf[BLKSIZE];

    //INIT ninodes, nblocks, bmap, imap, iblk
    get_block(dev, 1, buf);
    sp = (SUPER *)buf;

    printf("Checking EXT2 FS: ");
    if (sp->s_magic != 0xEF53)
    {
        printf("Not an ext2 FS\n");
        exit(1);
    }

    printf("OK\n");


    ninodes = sp->s_inodes_count;
    nblocks = sp->s_blocks_count;
    

    get_block(dev, 2, buf);
    gp = (GD *)buf;

    bmap = gp->bg_block_bitmap; // bmap block
    imap = gp->bg_inode_bitmap; // imap block
    iblk = gp->bg_inode_table; // inodes begin block

    printf("bmap = %d  imap = %d  iblock = %d\n", bmap, imap, iblk);


    // INIT minodes, procs
    // clear all MINODES
    // 
    for (int i = 0; i < NMINODE; i++)
    {
        mip = &minode[i];
        mip->dev = 0;
        mip->ino = 0;
        mip->refCount = 0;
        mip->mounted = 0;
        mip->mptr = 0;
    }

    // set root minode to 0
    root = 0;

    for (int i = 0; i < NPROC; i++) // initialize PROCs
    {
        p = &proc[i];
        p->pid = i + 1; // pid = 0 to NPROC-1
        p->uid = i; // P0 is a superuser process
        p->cwd = 0;

        for (int i = 0; i < NFD; i++)
        {
            p->fd[i] = 0; // all file descriptors are NULL
        }
        
    }
}

// will mount the root file system 
int mount_root()
{
    printf("mydisk mounted on / OK\n");
    // fill mount table mtable[0] with rootdev information
    root = iget(dev, 2);
    root->mounted = 1;

    //Let cwd of both P0 and P1 point at the root minode (refCount=3)
    running = &(proc[0]);
    running->cwd = iget(dev, 2);
    proc[1].cwd = iget(dev, 2);
    printf("creating P0, P1\n");
    // Let running -> P0.
    printf("root refcount = %d\n",root->refCount);
    printf("P0 running\n");
    
    return 0;
}


////////////// level one functions /////////////////


// will print out directory content
int my_ls()
{
    MINODE *mip;
    u16 mode;
    int dev1, ino;
    char pathname[64], *bname;

    // if pathname is empty, ls CWD
    if (path[0] == 0)
    {
        ls_dir(running->cwd);
    }
    // pathname is not empty ls pathname
    else
    {
        strcpy(pathname, path[0]);
        dev1 = root->dev;
        ino = getino(dev, pathname);

        // if ino = 0 -> file does not exit
        if (ino == 0)
        {
            printf("no such file %s\n", pathname);
            return -1;
        }

        // get MINODE ptr from dev and ino
        mip = iget(dev1, ino);
        
        // get mode of file
        mode = mip->INODE.i_mode;

        // if file is not a directroy, print its base content
        if (!S_ISDIR(mode))
        {
            bname = basename(pathname);

            // will print out info of basename of pathname
            ls_file(mip, bname);
        }
        // is a dir, recursively call function again.
        else
        {
            ls_dir(mip);
        }
        iput(mip);
    }
}

// will change CWD to pathname or root 
// or traverse back to up the file system using ..
int my_cd()
{
    char temp[256];
    char pathname[64];
    char buf[BLKSIZE];
    DIR *dp;
    MINODE *ip, *newip, *cwd;
    int dev, ino;
    char c;


    if (path[0] == 0) // if given no path
    {
        printf("changing Directory to root\n");
        iput(running->cwd);
        running->cwd = iget(root->dev, 2);
        return;
    }

    strcpy(pathname, path[0]);    

    if (pathname[0] == '/')
        dev = root->dev;
    else
        dev = running->cwd->dev;
    

    ino = getino(dev, pathname);

    if (!ino)
    {
        printf("cd : no such directory\n");
        return (-1);
    }

    if (debug)
        printf("dev=%d ino=%d\n", dev, ino);

    newip = iget(dev, ino); /* get inode of this ino */

    if (debug)
        printf("mode=%4x   ", newip->INODE.i_mode);

    
    if (!S_ISDIR(newip->INODE.i_mode))
    {
        printf("%s is not a directory\n", path[0]);
        iput(newip);
        return (-1);
    }

    printf("Changing directory to %s\n", path[0]);
    iput(running->cwd);
    running->cwd = newip;

    if (debug)
        printf("after cd : cwd = [%d %d]\n", running->cwd->dev, running->cwd->ino);

}

/* This function will recursively print out the path to the current
   working directory. It will take the minode of the current dir
   and find its parent inode and find its name, it will then repeat 
   the process until it has reached the root. ONce at the root, will
   print out the name of the parent dir, so the root then the child
   of root, then the child of the child, until the current dir has
   been reached. output: /parentdir/childdir
   */
int rpwd(MINODE * cwd)
{
    char buf[BLKSIZE], myname[256], *cp;
    MINODE *parent, *ip;
    u32 myino, parentino;
    DIR *dp;

    // if current MINODE is equal to the root
    // exit function and return to where it was
    // called from

    if (cwd == root)
        return;

    // fine INODE of parent directory and get INODE of current dir
    parentino = findino(cwd, &myino);
   
    // put INODE of parent into a MINODE
    parent = iget(dev, parentino);
    
    // using parent MINODE and INODE and current dir INODE
    // find name of current dir
    findmyname(parent, myino, myname);
    
    // recursively call rpwd() using parent INODE to traverse the 
    // back to the root, while getting the name of each dir
    rpwd(parent);

    // once the root has been reached, release MINODE
    // and print the name of the dir from the root to current dir
    // outcome: /parentdir/currentdir
    iput(parent);
    printf("/%s", myname);

    return 1;
}

// will print out the path to the current working directory
// using a recursive method
void my_pwd(MINODE *wd)
{
    if (wd == root)
    {
        printf("/\n");
        return;
    }
    rpwd(wd);
    printf("\n");
}


/* Will work like a hardlink function that linuz has.
   It will create a file that is identical to he oldfile 
   meaning the entry data will be the same as old file and
   only the name will differ.
*/
int my_link(char *oldf, char *newf)
{
    char new[64], old[64];
    char *newpath, *oldpath;
    char *bname, *dname;
    int old_ino, new_ino;
    MINODE *mip, *pip;
    int mode;

    // copy contents of newfile and oldfile 
    // read in from user input
    // to char arrays new and old
    strcpy(new, newf);
    strcpy(old, oldf);

    // duplicate new and old into newpath and oldpath
    newpath = strdup(new);
    oldpath = strdup(old);
   
    // get inode number of oldfile to use in new file
    // device will be the root->dev
    // old is pathname to old file
    old_ino = getino(dev, old);

    // if old file's inode number is 0, does not exist
    if(!old_ino)
        return 0;

    // use root->dev and old inode number to  get
    // INODE in memory
    mip = iget(root->dev, old_ino);

    // get mode of file using minode *ptr
    mode = mip->INODE.i_mode;

    // if file DIR, exit functions
    // cant hard link a DIR type
    if(S_ISDIR(mode))
    {
        printf("%s is a Directory\n", old);
        return -1;
    }

    // get basename and dirname of path to new file
    bname = basename(newpath);
    dname = dirname(new);

    // get inode number of the directory that will
    // contain hard link of old file
    // device is the root device
    //printf("dname = %s\n", dname);
    
    new_ino = getino(dev, dname);

    // if new inode number is 0, 
    // directory does not exist
    if(!new_ino)
    {
        printf("directory %s does not exist\n", dname);
        return -1;
    }

    // get minode of dirctory that will contain new file
    pip = iget(root->dev, new_ino);

    mode = pip->INODE.i_mode;

    // if mode of dname  is not a DIR
    // can't hard link because it cannot contain new file
    if(!S_ISDIR(mode))
    {
        printf("%s is not a directory\n", dname);
        return -1;
    }

    // if it is a dir type, search it for new file
    // if it exists exit function
    // else continue
    if(search(pip, bname))
    {
        printf("%s does exist in %s\n", bname, dname);
        return -1;
    }

    // add an entry to the data block of the directory
    // that will contain new file
    // will be assigned same inode number as old file
    enter_name(pip, old_ino, bname);

    // increase link count of old file
    // update old file access time
    // set old file dirty to 1
    mip->INODE.i_links_count++;
    mip->INODE.i_atime = time(0);
    mip->dirty = 1;
    
    // write INODE back to disk
    iput(mip);

    return 0;
}


/* Will decrement link count of the file given by user. 
   Deletes a name and possible the file it refers to.
   if link count is greater than 1 the entry is removed form
   parent but INODE data is not cleared
   if it is 0, clear data block and Inode from memory
*/
int my_unlink(char *oldsrc)
{
    char pathname[64], cpy[64], cpy2[64];
    char *dname, *bname;
    int ino, pino;
    int mode, count = 0;
    MINODE *mip, *pip;
    INODE *ip;


    // copy string from path[0] into pathname
    strcpy(pathname, oldsrc);
    strcpy(cpy2, pathname);
    strcpy(cpy, pathname);


    // get child and directory
    bname = basename(cpy2);
    dname = dirname(cpy);

    //******************* 1. Get file's minode ******************/ 

    // get inode number of file
    ino = getino(dev, oldsrc);
    

    // if ino is a zero, file does not exist
    if(!ino)
        return -1;
    
    // get MINODE of file
    mip = iget(dev, ino);

    // get the mode of the file
    mode = mip->INODE.i_mode;

    // if it is a directory, it is not possible to have links
    if((mode & DIR_MODE) == DIR_MODE)
    {
        printf("%s is a directory\n", pathname);
        return -1;
    }

    //************ 2. remove name entry form parent DIR's data block **********/

    // get INODE number of parent directory
    pino = getino(dev, dname);
    
    // get MINODE of parent inode
    pip = iget(dev, pino);

    printf("Unlinking %s\n", bname);
    
    // Remove childName using myrmchild()
    myrmchild(pip, bname);

    pip->dirty = 1;


    // decrement INODE's i_links_count by 1
    mip->INODE.i_links_count--;
    count = mip->INODE.i_links_count;
    
    // check if link count becomes zero
    if(count == 0)
    {
        // must rm pathname
        ip = &mip->INODE;

        // will deallocate its data blocks
        my_truncate(ip); 

        // Will deallocated INODE
        idealloc(dev, mip->ino); 
    }
    else // if not
    {
        mip->dirty = 1;
        mip->INODE.i_atime = time(0);
        mip->INODE.i_mtime = time(0);
    }

    return 0;
}

// When this function is called, it will create a file
// of type LNK that will be a symbolic link to a file
// will contain name of old file and size of file
// should be size of name of old file
int my_symlink()
{
	int ino, nino;
    int mode;
    char *parent, *base;
    char *nparent, *nbase;
    char b[64], d[64];
    char b2[64], d2[64];
	MINODE *mip, *nmip;

	//check the paths: exits function if format is incorrect
	if (path[0] == 0 || path[1] == 0)
	{
		printf(" usage symlink filename1 filename2\n");
		return -1;
	}
    
    // copy path[0] and path[1] into copies for new file and old file
    strcpy(b, path[0]); // oldfile
    strcpy(d, path[0]);
    strcpy(b2, path[1]);// newfile
    strcpy(d2, path[1]); 

    // get base and parent of new file
    nbase = basename(b2);
    nparent = dirname(d2);

    // get base and parent of old file
    base = basename(b);
    parent = dirname(d);

    // get INODE number of old file
    ino = getino(dev, path[0]);

    // check if it exists or not
    if(!ino)
        return 0;
    
    
    // get INODE into minode
    mip = iget(dev, ino);

    mode = mip->INODE.i_mode;

    // check if old file is a DIR or REG
    if(!S_ISDIR(mode) && !S_ISREG(mode))
    {
        printf("%s is neither a directory or regular file\n", base);
        return -1;
    }

    // since new file does not exist and old file is a dir or reg
    // creat new file and change its mode to LNK

    printf("Creating a symbolic link to %s\n", base);

    char *child = path[1];
    int status = CREAT(child);

    //printf("status = %d\n", status);

    // if zero, file exists
    if(status < 0)
    {
        return -1;
    }
    
    // Get INODE of new file
    nino = getino(dev, path[1]);


    // get INODE of new file into MINODE
    nmip = iget(dev, nino);

    
    nmip->INODE.i_mode = 0120777; // set new file to a LNK type
    

    // will set file size to length of old_file name
    // store old file name in new file's INODE.block[] area
    //printf("path[0] = %s\n", base);

    enter_name_link_file(nmip, path[0]);

    nmip->dirty = 1;
    iput(nmip);

    return 0;

}

/*  Will take dev and ino and copy into stat data structure fields.
    Will also copy INODE data structure content inot stat data struct content.
    Will work just like stat(char *file, struct stat fstat);
*/
int my_stat()
{
    struct stat fstat;
    int ino, i;
    char *pathname[64];
    INODE *ip;
    MINODE *mip;

    if(path[0] == 0) // given no file
    {
        printf("usage: stat filename\n");
        return -1;
    }


    dev = root->dev;
    ino = getino(dev, path[0]); 

    // if ino is 0, can't stat file
    // does not exist in FS
    if(ino == 0)
    {
        return 0; // file does not exits, so it can't be stat
    }

    // get MINODE ptr to an in-memory INODE corresponding to file
    mip = iget(dev, ino);

    // set an INODE ptr to point
    // to the INODE data struct pointed 
    // to by mip
    ip = &mip->INODE;
    
    // fill up struct data fields
    fstat.st_dev = mip->dev;
    fstat.st_ino = mip->ino;
    fstat.st_mode = ip->i_mode;
    fstat.st_nlink = ip->i_links_count;
    fstat.st_uid = ip->i_uid;
    fstat.st_gid = ip->i_gid;
    fstat.st_size = ip->i_size;
    fstat.st_atime = ip->i_atime;
    fstat.st_ctime = ip->i_ctime;
    fstat.st_mtime = ip->i_mtime;
    fstat.st_blocks = ip->i_blocks;
    iput(mip);
    
    // print stat content to screen
    print_stat(fstat, basename(path[0]));

    return;
}

/*
    This function will take in a filename and a mode
    in hexidecimal format. It will change the filename mode
    to the given mode. meaning it will change the permissions 
    for the user, other, and group. 
    So if new mode is 0777, the permission will be: rwx rwx rwx
*/
int my_chmod()
{
    MINODE *mip;
    struct stat fstat, *sp = &fstat;
    int ino;
    int dev;
    int mode = strtol(path[0], 0, 8);
    int cMode;
    char file;
    
    if(debug)
    {
        printf("mode = %o\n", mode);
        printf("path = %s\n", path[1]);
    }

    // check if not given a filename or mode
    if(path[1] == 0 || path[0] == 0)
    {
        printf("usage: chmod mode filename\n");
        return 1;
    }


    dev = root->dev;
    // get filename INODE in memory
    ino = getino(dev, path[1]);

    if(debug)
        printf("ino = %d\n", ino);

    // check if inode number returned is 0
    // if so, file does not exits
    if(ino == 0)
    {
        printf("File Does not exit\n");
        return -1;
    }

    // get MINODE pointer of file to an in-memory INODE of the file
    mip = iget(dev, ino);

    // now have access to st_mode
    // |= -> mode= mode | value

    // set file mode and or with new mode
    mip->INODE.i_mode |= mode;



    // stat that INODE has been modified
    mip->dirty = 1;
    
    iput(mip);
    return 0;
}


/* Will take in a filename and create an emtpy file with the given name 
   if it does exist, will print warning and will not creat file specified*/
int CREAT(char * pathname)
{
	int parentNode;
	char *parent, *child;
	char temp[64], temp2[64];
	int status, mode;

	// if not given any pathname, exit
	if (pathname == 0)
	{
		//pathname was null, cant make a directory
		printf("Failed to make a directory, pathname not supplied.\n");
		return -1;
	}

	// get dirname and basename from pathname given
	strcpy(temp, pathname);
	strcpy(temp2, pathname);
    
	parent = dirname(temp);
	child = basename(temp2);

	if(debug)
	{
		printf("temp = %s, temp = %s\n", temp, temp2);
		printf("parent = %s child = %s\n", parent, child);
	}

	// if dirname starts with /, start from the root directory
	if (parent[0] == '/')
	{
		dev = root->dev; //set root
	}
	else // else dirname does not start with /, start from CWD
	{
		
		dev = running->cwd->dev; //set cwd
	}

    /// get parent Directory INODE
    parentNode = getino(dev, parent);

    // if parentNode is zero, parent does not exist
    if(!parentNode)
        return -1;

	//get the IN_MEMORY minode of the parent
	//pip points at the parent minode[] of dirname
	MINODE *pip = iget(dev, parentNode);

    // get mode of parent
    mode = pip->INODE.i_mode;

	//verify: (1) parent INODE is a DIR
	if ((mode & DIR_MODE) != DIR_MODE)
	{
		printf( "Failed to make a file, specified parent directory %s is not a directory.\n", parent);
		return -1;
	}

	//verify: (2) child does NOT exists in the parent directory
	if (search(pip, child))
	{
		printf("Failed to make a file. The specified child %s already exists.\n", child);
		return -1;
	}


	//call mycreat
    printf("creating %s\n", child);
	status = mycreat(pip, child);

	//Touch its atime and mark it DIRTY
	pip->dirty = 1;
	iput(pip);
    printf("DONE...\n");
	return status; //0 if mymkdir was successful
}

/*
    will create an empty file if it does not exit,
    otherwise it will simple update access time
    Will check if given file exits.
    if it does exist, will simply update access and modification time
    if it does not, will call creat and make a new file
*/
int my_touch()
{
	char temp[64], cpy[64], *bname;
    int ino;
    MINODE *mip;

    strcpy(temp, path[0]);
    
    
    // Will check if a pathname is given
    // if not, it will exits and show
    // format required
    if(temp[0] == 0)
    {
        printf("usage: touch filename\n");
        return 0;
    }
    
    // get inode of pathname
    dev = root->dev;
    ino = getino(dev, temp);
    strcpy(cpy, temp);
    bname = basename(cpy);

   
    if(ino == 0)
    {
        // file does not exist, create one
        // will create 
        printf("Will create %s\n",bname);
        CREAT(temp);
    }
    else
    {
        // file does exist, update time
        printf(" %s does exist\n", bname);
        mip = iget(dev, ino);
        mip->INODE.i_atime = time(0L); 					 // updata access time to current
	    mip->INODE.i_mtime = time(0L); 				   // updata modified time to current
        mip->dirty = 1;
    }
    
    return 0;
}


// This function will simply exit the program when it is called
int quit ()
{
    // will go through all MINODES and if refcount is 0 and marked dirty
    // write back to disk and clear
    for (int i = 0; i < NMINODE; i++)
    {
        if (minode[i].refCount > 0 && minode[i].dirty)
        {
            iput(&minode[i]);
        }
    }
    printf("quiting....\n");
    exit(0);
}   

// Will print out all available functions that FS can use
// will also show the proper format for each function
void menu()
{
    printf("*************************** COMMAND MENU *****************************\n");
    printf("mkdir       rmdir       ls        cd         pwd     creat       touch\n");
    printf("creat       link        unlink    symlink    stat    chmod       open \n");
    printf("close       read        write     lseek      cat     cp          mv   \n");
    printf("                               quit\n;");

    printf("=========================== USAGE EXAMPLES ============================\n");
    printf("mkdir filename\n");
    printf("rmdir filename\n");
    printf("ls filename\n");
    printf("cd filename \n");
    printf("touch filename\n");
    printf("creat filename\n");
    printf("link oldfile newfile\n");
    printf("unlink filename\n");
    printf("stat filename\n");
    printf("chmod mode filename\n");
    printf("open filename mode -> mode = 0|1|2|3 for R|W|RW|AP\n");
    printf("close fd\n");
    printf("write fd text\n");
    printf("lseek fd position\n");
    printf("cat src dest\n");
    printf("mv src dest\n");
}

///////////////////////////////////////////////////////////


/* main.c */
int main(int argc, char * argv[])
{
    int ino, index;
    char buf[BLKSIZE];
    char line[256]; // will contain both command 
    
    
    // if format is not right, show correct format and exit
    if(argv < 1)
    {
        printf("Usage: a.out mydisk\n");
        exit(1);
    }

    // correct format was entered
    disk = argv[1]; // set diskimage to disk
    
    // open diskimage for reading|writing
    fd = open(disk, O_RDWR);
    if(fd < 0)
    {
        printf("%s does not exit\n");
        exit(1);
    }

    dev = fd;

    // disk exists
    // initialize data structures and mount root
    printf("fs_init()\n");
    printf("mount_root\n");

    fs_init();
    mount_root();

    debug = RUN; // for testing purposes

    // run program
    while(1)
    {   
        for(int i = 0; i < 64; i++ )
            name[i] = 0;
        for(int i = 0; i < 32; i++)
            path[i] = 0;
        cmd  = 0;
        
        // prompt user for cmd
        printf("==============================================================================\n");
        printf("SKYNET@USER: ");
        fgets(line,128,stdin);
        line[strlen(line)-1] = 0;

        // sparse the command line
        // call tokensize to get cmd, pathnames and/or mode        
        parseline(line);
        

        if(debug)
        {
            printf("line = %s\n",line );
            printf("cmd = %s\n", cmd);
            if(strcmp(cmd, "chmod") == 0)
             {   
                 printf("mode = %s\n", path[0]);
                 printf("path = %s\n", path[1]);
             }
            else
                printf("path = %s\n", path[0]);
        }

        // check if cmd is one of commands listed
        index = get_cmd_index(cmd);

        // base on index, will call function that 
        // is associated with index
        switch(index)
        {
            case 0: my_mkdir(); break;
            case 1: my_rmdir(); break;
            case 2: my_ls(); break;
            case 3: my_cd(); break;
            case 4: my_pwd(running->cwd); break;
            case 5: CREAT(path[0]); break;
            case 6: my_link(path[0], path[1]); break;
            case 7: my_unlink(path[0]); break;
            case 8: my_symlink(); break;
            case 9: my_stat(); break;
            case 10: my_chmod(); break;
            case 11: my_touch(); break;
            case 12: my_readlink(); break;
            case 13: menu(); break;
            case 14: pfd(); break;
            case 15: my_open(path[0],path[1]); break;
            case 16: my_close(path[0]); break;
            case 17: my_read(path[0], path[1]); break;
            case 18: my_write(line); break;
            case 19: my_lseek(line); break;
            case 20: my_cat(); break;
            case 21: my_cp(line); break;
            case 22: my_mv(line); break;
            case 23: quit(); break;
            default: printf("command is invalid\n"); 
        }
    }
    
    return 0;
}



