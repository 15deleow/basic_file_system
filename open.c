#include "head.h"
// Global Variables
extern MINODE minode[NMINODE];
extern MINODE *root;
extern PROC   proc[NPROC], *running;
extern OFT oft[NOFT];

extern char gpath[128];
extern char *name[64];


extern int fd, dev, iblk;

extern int nblocks, ninodes, bmap, imap, inode_start;
extern char line[256];

extern char *cmd; // will contain command string
extern char * path[32]; // will contain pathname string and/or file mode
extern int debug; // for debuggin code


//function that user calls to give fd, mode in, offset, and INODE # of a file
int pfd() 
{
   printf("   fd     mode     offset    INODE\n");
   printf("  ----    ----     ------   -------\n");
   char mode[10];

   for(int i = 0; i < 10; i++)
   {
       if(!running->fd[i])
            continue;
        switch(running->fd[i]->mode)
        {
            case(0):
                strcpy(mode, "READ");
                break;
            case(1):
                strcpy(mode, "WRITE");
                break;
            case(2):
                strcpy(mode, "RW");
                break;
            case(3):
                strcpy(mode, "APPEND");
                break;
        }
         printf("    %d    %s       %d      [%d, %d]\n",i, &mode, running->fd[i]->offset, dev, running->fd[i]->mptr->ino);
   }
}

// Will open a file for R or W or WR or AP
// will allocated a OFT struct of this file
// once allocated, will fill its data fields
// will the set OFT array and proc OFT = to
// allocated OFT struct
int my_open(char * path1,char * path2)
{
    int ino;
    int mode = strtol(path2,0,10);
    char filename[64];
    strcpy(filename,path1);
    printf("name = %s\n", filename);

   

    if(mode < 0 || mode > 4) //only modes 0-3
    {
        printf("invalid fd mode\n");
        return -1;
    }

     switch(mode)
    {
        case 0: printf("mode = READ\n"); break;
        case 1: printf("mode = WRITE\n"); break;
        case 2: printf("mode = READ/WRITE\n"); break;
        case 3: printf("mode = APPEND\n"); break;
    }

    //wr/create file doesnt need to exist
    if(mode != 1) 
    {
            ino = getino(dev, filename);
           if(!ino)
           {
               printf("file does not exit\n");
               return -1;
           }

    }
    else
    {
        ino = getino(dev, filename);
        //printf("ino # = %d\n", ino);
        if(!ino)
        {
            //printf("ENTER IF STATEMENT\n");
            CREAT(filename);
            ino = getino(dev, filename);
        }

    }

    if(!ino)
    {
        printf("Disk out of space\n");
        return -1;
    }

    //STEP 1: Get file's Minode
    MINODE *mip = iget(dev,ino);
    //printf("File type: %x\n", mip->INODE.i_mode);

    int mode1 = mip->INODE.i_mode;

    if( (mode1 & FILE_MODE) != FILE_MODE)
    {
        printf("is not reg file\n");
        return -1;
    }

    //printf("MINODE INODe = %d\n", mip->INODE.i_)

    for(int i= 0; i < 10; i++) //checked to see if the file alredy open
    {
        if(running->fd[i] != 0)
        {
            if(running->fd[i]->mptr == mip)
                if(running->fd[i]->mode == 0 || running->fd[i]->mode == 1 || running->fd[i]->mode == 2 || running->fd[i]->mode == 3)
                {
                    printf("file is already open as INCOMPATIBLE\n");
                    iput(mip);
                    return -1;
                }
        }
    }
    //STEP 2: allocate open table entry OFT, initailize OFT entries
    OFT *myoft = malloc(sizeof(OFT));
    myoft->mode = mode;
    myoft->mptr = mip;
    myoft->refCount = 1;
    switch(mode)
    {
        case (0): //R
            myoft->offset = 0;
            break;
        case (1): //W
            //printf("IT HAS ENTERED\n");
            my_truncate(&mip->INODE);
            myoft->offset = 0;
            break;
        case (2): //RW
            myoft->offset = 0;
            break;
        case (3): //APPEND
            myoft->offset = mip->INODE.i_size;
            break;
        case (4): //WR/CREATE
            myoft->offset = 0;
            break;
        default:
            printf("Invalid mode\n");
            return -1;
    }

    //STEP 3: search for first free FD entry and lowest index of proc
    //search for frist free fd 
     for(int i = 0; i < NOFT; i++)
    {
        if(oft[i].mptr == 0)
        {
            oft[i] = *myoft;
            break;
        }
    }
    // add to proc's fd list lowest index of proc
    int i;
    for(i = 0; i < NFD; i++)
    {
        if(running->fd[i] == 0)
        {
            running->fd[i] = myoft;
            break;
        }
    }

// Touch files time and mark dirty
    if(mode == 4)
        mip->INODE.i_ctime = time(0L);
    else if(mode == 1 || mode == 2 || mode == 3)
        mip->INODE.i_mtime = time(0L);
    mip->INODE.i_atime = time(0L);
    mip->dirty = 1;
    iput(mip);

    //return index of file descriptor
    return i; //file descriptor

}

// Will close a file by:
/*
    Will clear the OFT fd[] structs at the index
    denoted by fd. Will decrease ref count by 1
    if ref count is > 0, file is being used still
    else, release the MINODE and set dirty to 1
*/
int my_close(char * path)
{
    int fd = strtol(path, 0, 10);
    printf("FD = %d\n", fd);

    // check fd valid opened file descriptor
    if(fd < 0 || fd >= 10 || running->fd[fd] == 0)
    {
        printf("Invalid fd\n");
        return -1;
    }

    // set OFT pointer equal to process oft struct at index fd in the struct array
    OFT *oftp = running->fd[fd];

    //find which value of oft in array MINODE needs to be cleared
    for(int i = 0; i < NOFT; i++)
        if(oftp == &oft[i])
            oft[i].mptr = 0;

    // clears oft struct fd at index fd.
    running->fd[fd] = 0;

    oftp->refCount--; // decrease ref count to zero

    if(oftp->refCount > 0) // still in use
        return 0;
    
    //last user of this oft entry. dispose of minode
    MINODE *mip = oftp->mptr;
    mip->dirty = 1;
    iput(mip);
    return 1;

}


// same as my_close
int cat_close(int fd)
{
    // printf("FD = %d\n", fd);
    // check fd valid opened file descriptor
    if(fd < 0 || fd >= 10 || running->fd[fd] == 0)
    {
        printf("Invalid fd\n");
        return -1;
    }
    OFT *oftp = running->fd[fd];

    //find the which value of oft in array MINODE needs to be deserted
    for(int i = 0; i < NOFT; i++)
        if(oftp == &oft[i])
            oft[i].mptr = 0;
    running->fd[fd] = 0;

    oftp->refCount--;
    if(oftp->refCount > 0) // still in use
        return 0;
    //last user of this oft entry. dispose of minode
    MINODE *mip = oftp->mptr;
    mip->dirty = 1;
    iput(mip);
    return 1;
}




