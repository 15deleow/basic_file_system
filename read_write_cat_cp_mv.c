#include "head.h"

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


// Will read contents of file denoted by fd up nbytes
// writes it to buf
// will use offset to find location in block
// once physical iblk number is found,
// will be read into readbuf;
// Will then read up to max size of buf or up to remaining bytes in readbuf;
// once number of bytes to read has reached zero.
// return and print buf to screen.
int myread(int fd, char buf[], int nbytes)
{
    int count = 0, avail, lblk, startByte, filesize, remain, blk, numBytesToCopy;
    char readbuf[BLKSIZE], dbuf[BLKSIZE], indirect[BLKSIZE];
   
    OFT * oftp = running->fd[fd];       // set a OFT pointer to point to running->fd[fd], which is a OFT struct that is within the area fd[] in proc struct
    MINODE *mip = oftp->mptr;          //  set a MINODE ptr to the oftp->mptr, which is a pointer to minode of file
    
    int *pl;

    // if mip is zero, the fd is invalid
    // fd is not in use
    if(!mip)
    {
        printf("invalid fd!\n");
        return 0;
    }

    // set filesize to the size of the file being read
    filesize = mip->INODE.i_size;

    // available text to be read
    avail = filesize - oftp->offset;

    // while number of bytes to be read and available text to read are greater than zero
    while(nbytes && avail)
    {
        //lblk = offset divided by 1024;
        // compute logical block
        lblk = oftp->offset / BLKSIZE;

        //startByte = starting point in file mod 1024
        // starting point within the block
        startByte = oftp->offset % BLKSIZE;
        
        // if the offset is >= the file size
        // out of scope of the block
        if(oftp->offset >= mip->INODE.i_size)
            return 0;

        // check if lblk is a direct block
        if(lblk < 12)
        {
            blk = mip->INODE.i_block[lblk];
        }
        // check if starting is in the indirect blocks
        else if(lblk >= 12 && lblk < 256 + 12) // indirect block
        {
            get_block(mip->dev, mip->INODE.i_block[12], dbuf);
            int *cp = dbuf;
            blk = cp[lblk - 12];
        }
        else // double indirect block
        {
            get_block(mip->dev, mip->INODE.i_block[13], dbuf);
            pl = (int *)dbuf + ((lblk - 268) / 256);
            get_block(mip->dev, *pl, dbuf);
            pl = (int *)dbuf +((lblk - 268) % 256);
            blk = *pl;
        }

        // get data block into readbuf 
        get_block(mip->dev, blk, readbuf);

        // copy from start byte to buf[]
        char *cp = (readbuf + startByte);

        // remaining bytes to read
        remain = BLKSIZE - startByte;
        
        if(nbytes < remain) //only copy numBytes left to read
            numBytesToCopy = nbytes;
        
        else // copy rest of block data to buf
            numBytesToCopy = remain; 

        memcpy((buf + count), cp, numBytesToCopy); // appends to text already written in buf

        oftp->offset += numBytesToCopy; // increase offset by numBytesTocopy

        count += numBytesToCopy; // incease count by numBytesToCopy

        avail -= numBytesToCopy;

        nbytes -= numBytesToCopy;

    }

    // if the offsie it > than the file size set offset to file size
    if(oftp->offset > oftp->mptr->INODE.i_size)
        oftp->offset = oftp->mptr->INODE.i_size;
    
    // release MINODE
    iput(mip);
    
    return count;
}


// This function will read from a file up to the number of bytes the user specifices.
// Will then print the read content to screen.
// First it will creat a cahr buf[] of size of bytes to read in + 1
// Will call myread, which will read file and write to buf
// Then print buf to screen
int my_read()
{
    int fd = strtol(path[0], 0, 10); // convert string input to long int, for use

    int numBytes = strtol(path[1], 0, 10); // get number of bytes to read from string to integer

    char buf[numBytes + 1]; // initialize a char array of size numbyte + 1

    strcpy(buf, " ");

    int n = myread(fd, buf, numBytes);

    buf[n] = 0;

    printf("%s\n", buf);
    return n;
}


// Will be called by my_write() and will write buf[] into the actual file
// will search for the next available memory space in the file.
// will traverse the direct, indirect, and double indirect if needed. 
// Will allocate a block if no i_blk exists at location determined by i_blk[lblk]
// ONce everything is set, will write buf to file
int mywrite(int fd, char buf[], int nbytes)
{
    int count = 0, avail, lblk, startByte, filesize, remain, blk, size, numBytesToCopy;
    OFT * oftp = running->fd[fd];
    MINODE *mip = oftp->mptr;
    int ibuf[256];
    char wbuf[BLKSIZE];
    unsigned long w;
    int *blkPtr;

    // if mip is 0, it is an empty inode so the FD was invalid 
    // not one of the avaliable fd
    if(!mip)
    {
        printf("invalid fd!\n");
        return 0;
    }
    
    char readbuf[nbytes], dbuf[BLKSIZE], indirect[BLKSIZE];

    // get size of the file
    filesize = mip->INODE.i_size;
    

  
    // check where the next open block in memory 
    while(nbytes > 0)
    {   
        // get iblock location and where it starts
        lblk = oftp->offset / BLKSIZE;
        startByte = oftp->offset % BLKSIZE;

        // set ibuf[] = {NULL}
        memset(ibuf, 0, BLKSIZE);

        // if lblk is less than 12, file is small and only 
        if(lblk < 12)
        {
             // if i_block[i] has not been allocated
            if(mip->INODE.i_block[lblk] == 0)
            {
                mip->INODE.i_block[lblk] = balloc(mip->dev); // allocate a INODE block
                memset(wbuf, 0, BLKSIZE);                    // set wbuf[] = {NULL}
                put_block(mip->dev, mip->INODE.i_block[lblk], wbuf); //clear block pointing to
            }

            blk = mip->INODE.i_block[lblk]; // get the INODE block denoted by lblock
        }
        else if(lblk >= 12 && lblk < 256 + 12) // indirect block
        {
            
            if(mip->INODE.i_block[12] == 0)
            { 
                // no indirect blocks yet
                // allocate a new block for block 12
                mip->INODE.i_block[12] = balloc(mip->dev);
                memset(ibuf, 0, BLKSIZE);
                put_block(mip->dev, mip->INODE.i_block[12], ibuf); // clear indirect block
            }
            // open iblock 12 to get to the 256 blocks
            get_block(mip->dev, mip->INODE.i_block[12], ibuf);

            // will get first iblock in the indirect block
            blk = ibuf[lblk - 12];

            if(blk == 0)
            { 
                // block does not exist yet
                ibuf[lblk-12] = balloc(mip->dev);
                blk = ibuf[lblk-12];
                put_block(mip->dev, mip->INODE.i_block[12], ibuf);
            }
        }
        else // double indirect block
        {
            if(mip->INODE.i_block[13] == 0)
            {
                mip->INODE.i_block[13] = balloc(mip->dev);
                memset(ibuf, 0, BLKSIZE);
                put_block(mip->dev, mip->INODE.i_block[13], ibuf);
            }
           
            get_block(mip->dev, mip->INODE.i_block[13], ibuf);

            blkPtr =(int *)ibuf +((lblk-268)/256);
           
            blk = *blkPtr;
            
            // int b = cp + cp[(lblk - 12) % 256];

            if(!blk) //blk is zero
            {
                *blkPtr = balloc(mip->dev); // allocated a block and blk number is pointed by blkptr
                blk = *blkPtr; // set new  blk number to blk
                put_block(mip->dev, blk, ibuf); // write blck back to disk
                memset(ibuf,0,BLKSIZE); // clear ibuf[]
                put_block(mip->dev, *blkPtr, ibuf);  // write buf back to same spot
              
            }
            
            // open the blk in the 256 specified blocks pointed by blk 13
            get_block(mip->dev, blk, ibuf);

            int newblk = blk; // newblk is equal to blk

            blkPtr = (int *)ibuf+((lblk-268)%256); //shift in block to 

            blk = *blkPtr;

            if(!blk)
            {
                *blkPtr = balloc(mip->dev);
                blk = *blkPtr;
                put_block(mip->dev, newblk, ibuf);

                memset(ibuf,0,BLKSIZE);

                put_block(mip->dev,*blkPtr,ibuf);
            }
        }

        // get data block into wbuf  
        get_block(mip->dev, blk, wbuf);
        
        // copy from start byte to buf[]
        char *cp = wbuf + startByte;
        char *cq = buf;

        remain = BLKSIZE - startByte;
        memset(cp, 0, remain);

        // If buffer is less than remaining bytes, only copy up to the length of buf
        if(strlen(buf) < remain)
            numBytesToCopy = strlen(buf);

        else // ifd remain is less than size of buf, can only write up to remain bytes
            numBytesToCopy = remain;

        memcpy(cp, cq, numBytesToCopy); // copy cq into cp and upto numBytes to copy in actuall memory

        oftp->offset += numBytesToCopy; // move over number of bytes

        size += numBytesToCopy; // increase size by numBytesToCopy

        nbytes -= numBytesToCopy; // will decrease the number of bytes to write in by number of bytes copied

        remain -= numBytesToCopy; // decrease remaing bytes in blk

        put_block(mip->dev, blk, wbuf); // write wbuf into Iblock
        
        if(nbytes <= 0)
            break;
    }


    mip->dirty = 1;
    mip->INODE.i_size = oftp->offset;
    iput(mip);
    


    return size;
}

/* This function will write a set of characters to the file
   of the file denoted by fd. Will check copy user input to
   char buffer and call mywrite.
*/
int my_write(char *line)
{
    char *cmd;
    char * fileD;
    char * string1[64];
    char buf[100];
    char * token;
    int i = 0;

    /* Used to parse line and get the string we want to write into file.
       Also, get the fd value that denoted the file */

     token =strtok(line, " \t");
     cmd = token;

     while(token)
     {
         token = strtok(NULL, " \t");
         string1[i] = token;
         i++;
     }
    
    fileD = string1[0];
    strcpy(buf, string1[1]);
    i = 2;
    while(string1[i] != NULL)
    {
        strcat(buf, " ");
        strcat(buf, string1[i]);
        i++;   
    }
    
    // check for proper format
    if(fileD == 0)
    {
        printf("USAGE: write FD text\n");
        return -1;
    }

 
    int fd = strtol(fileD, 0, 10); // covert fileD to integer

   

    // if fd is not one of the available fd or mode is zero or running->fd[fd] is 0
    if(fd < 0 || fd > 9 || running->fd[fd] == 0 || running->fd[fd]->mode == 0)
    {
        printf("cannot write to read fd\n");
        return -1;
    }

    int n = strlen(buf);        // get size of buf  
    printf("Writing %s to file...\n", buf);
    n = mywrite(fd, buf, n);   //  call mywrite which will right buf inot file

    printf("Writing Complete\n");
    return n; 
}


/* 
    will check if file descriptor is valid (less than NFD) and in use.
    IF so continue and check if position exceeds file size.
    If not, set original position to cuurent offset, then set
    offset to postion. return original position. 
*/
int my_lseek(char * line)
{
    char cmd[64];
    char fileD[64];
    char postion[64];
 
    // sparse line to get cmd, FD, and postion offset
    sscanf(line, "%s %s %s", cmd, fileD, postion);

    // convert strings to integer
    int fd = strtol(fileD, 0, 10);

    int position = strtol(postion, 0, 10);

    int originalPosition;

    // STEP 1:  check if fd is valid
    if(fd > NFD)
    {
        printf("invalid fd\n");
        return -1;
    }

    //STEP 2: check if FD exists in proc->running->OFT struct array
    // if OFT pointer is equal to fd[fd], which is a pointer to a FD
    OFT *oftp = running->fd[fd];
    
    // if oftp is NULL, file descriptor is not being used, exit function
    if(!oftp)
    {
        printf("fd %d not in use\n", fd);
        return -1;
    }

    // 2. Check if position given does not exceed file size
    // if the postion is greater the size of the file
    // error, can't move past size of file
    if(position >= oftp->mptr->INODE.i_size)
    {
        printf("position greater than file size\n");
        return -1;
    }

    // 3. update offset with new location
    // set original postion to offset value
    originalPosition = oftp->offset;
    
    // set offset to new position, this is the starting place
    // when reading file
    oftp->offset = position;

    return originalPosition;
}

/* Will open and read the contents of a file. Then 
   it will print the data to the screen to be seen
   by user
*/
int my_cat()
{

    //printf("Path = %s", path[0]);
    char file[100];
    
    char buf[BLKSIZE], dummy = 0;
    int n, fd;
    int total = 0;
    
    strcpy(file,path[0]); // copy string from path[0] to file
   
    // open file for reading
    fd = my_open(file, "0");
    
    // if fd is zero, failed to open file
    if(fd < 0)
    {
        printf("unable to open file\n");
        return;
    }

    // will print contents of file to screen 
    while(n = myread(fd, buf, BLKSIZE))
    {
        total += n;
        buf[n] = 0;
        printf("%s", buf);
        if(total > running->fd[fd]->mptr->INODE.i_size)
            break;
    }
    printf("\n");
    cat_close(fd); // close the file
    return 0;
} 


/* Will open a file for reading and a file for writing, even if it does not exist.
   Will read data from file1 and write it to file2.
*/
int my_cp(char * line)
{
    char cmd[64];
    char src[64];
    char dest[64];
    int n;
    char buf[BLKSIZE];

    sscanf(line, "%s %s %s %s", cmd, src, dest);

    printf("cmd = %s src = %s dest = %s\n", cmd, src, dest);

    // open src for reading
    int fd = my_open(src, "0");

    if(fd < 0)
    {
        printf("Could not access file\n");
        return -1;
    }

    // open destination for writing or create
    int gd = my_open(dest, "1");

    if(gd < 0)
        return -1;
    
   // will read from fd and will write to gd
   // once everything no more data is read from file
   // will break from loop
    while(n = myread(fd, buf, BLKSIZE))
    {
        buf[n] = 0;
        mywrite(gd, buf, strlen(buf));
    }

    //printf("breakpoint \n");
    // will close both files
    cat_close(fd);
    cat_close(gd);
}


// Will move one file to another locataion and then unlink it from its current postion
int my_mv(char *line)
{
    char cmd[64];
    char path[64];
    char text[64];

    sscanf(line, "%s %s %s %s", cmd, path, text);

   // printf("cmd = %s fd = %s string = %s\n", cmd, path, text);

    int ino = getino(dev, path);

    //printf("path[0] = %s %s\n", path, text);

    if(!ino)
    {
        printf("src does not exist\n");
        return -1;
    }
    printf("Moving %s to %s\n", path, text );

     my_link(path, text);
    
     MINODE *mip = iget(dev, ino);
     mip->INODE.i_links_count--;
     my_unlink(path);

    return 0;
}