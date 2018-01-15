/*********** Exercise 1: display the group descriptor ************/
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <ext2fs/ext2_fs.h>
#include <string.h>
#include <libgen.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/types.h>
#include <unistd.h>

#include "types.h"

MINODE minode[NMINODE];
MINODE *root;
PROC   proc[NPROC], *running;
MTABLE mtable[4];

SUPER *sp;
GD    *gp;
INODE *ip;

int dev;	 //the disk we are using
int nblocks; // from superblock
int ninodes; // from superblock
int bmap;    // bmap block 
int imap;    // imap block 
int iblock;  // inodes begin block


/************************************************************************
*						Helper functions
***********************************************************************/
// tests if a bit is avalable in the buf
int tst_bit(char *buf, int bit) {
	return buf[bit / 8] & (1 << (bit % 8));
}

//sets a given bit in the buf
void set_bit(char *buf, int bit) {
	buf[bit / 8] |= (1 << (bit % 8));
}

//takes in block number then reads block from device into buf
int get_block(int dev, int blk, char *buf){
	lseek(dev, (long)blk*BLKSIZE, SEEK_SET);
	return read(dev, buf, BLKSIZE);
}

//takes in block number and writes the data from buf onto the devive
int put_block(int dev, int blk, char *buf){
	lseek(dev, (long)blk*BLKSIZE, SEEK_SET);
	return write(dev, buf, BLKSIZE);
}

//decrements the free inodes count in the SUPER and Group Descriptor blocks
	//Used for creating a file or directory on the file system
int decFreeInodes(int dev){
	char buf[BLKSIZE];
	SUPER *spTmp;
	GD *gpTmp;

	get_block(dev, 1, buf);
	spTmp = (SUPER *)buf;
	spTmp->s_free_inodes_count--;
	put_block(dev, 1, buf);
	get_block(dev, 2, buf);
	gpTmp = (GD *)buf;
	gpTmp->bg_free_inodes_count--;
	put_block(dev, 2, buf);
}

//decrements the free data blocks count in the SUPER and Group Descriptor blocks
	//Used for creating a file or directory on the file system
int decFreeDataBlocks(int dev){
	char buf[BLKSIZE];
	SUPER *spTmp;
	GD *gpTmp;

	get_block(dev, 1, buf);
	spTmp = (SUPER *)buf;
	spTmp->s_free_blocks_count--;
	put_block(dev, 1, buf);
	get_block(dev, 2, buf);
	gpTmp = (GD *)buf;
	gpTmp->bg_free_blocks_count--;
	put_block(dev, 2, buf);
}

//allocates an inode in the imap block on the device
	//RETURNS: the inode number of the newly alloced inode, 0 if there are no more free inodes 
int ialloc(int dev){
	char buf[BLKSIZE];
	get_block(dev, mtable[0].imap, buf);
	//scan the whole imap block
	for (int i = 0; i < mtable[0].ninodes; i++) {
		//if an unused (0) inode is found, allocate it and mark it in-use (1)
		if (tst_bit(buf, i) == 0) {
			set_bit(buf, i);		//sets the first unused (0) inode to in-use (1)
			put_block(dev, mtable[0].imap, buf);	//write updated imap block back to the device
			decFreeInodes(dev);	// update free inode count in SUPER and GD
			return (i + 1);
		}
	}
	return 0; // out of FREE inodes
}

//allocates a data block in the bmap block on the device
	//RETURNS: the data block number of the newly alloced data block, 0 if there are no more free data blocks 
int balloc(int dev){
	char buf[BLKSIZE];
	get_block(dev, mtable[0].bmap, buf);
	//scan the whole bmap block
	for (int i = 0; i < mtable[0].nblock; i++) {
		//if an unused (0) data block is found, allocate it and mark it in-use (1)
		if (tst_bit(buf, i) == 0) {
			set_bit(buf, i);		//sets the first unused (0) data block to in-use (1)
			put_block(dev, mtable[0].bmap, buf);	//write updated bmap block back to the device
			decFreeDataBlocks(dev);	// update free data block count in SUPER and GD
			return (i + 1);
		}
	}
	return 0; // out of FREE data blocks
}

void clr_bit(char *buf, int bit) // clear bit in char buf[BLKSIZE]
{   buf[bit / 8] &= ~(1 << (bit % 8)); }

// INODE  de-Allocate
void idalloc(int dev, int ino){
	int i;
	char buf[BLKSIZE];
	if (ino > mtable[0].ninodes){ // niodes global
		printf("inumber %d out of range\n", ino);
		return;
	}
	// get inode bitmap block
	get_block(dev, mtable[0].imap, buf);
	clr_bit(buf, ino-1);
	// write buf back
	put_block(dev, mtable[0].imap, buf);
	// update free inode count in SUPER and GD
	incFreeInodes(dev);
}

// Data Block de-Allocate
void bdalloc(int dev, int bno){
	int i;
	char buf[BLKSIZE];
	if (bno > mtable[0].nblock){ // niodes global
		printf("bnumber %d out of range\n", bno);
		return;
	}
	// get inode bitmap block
	get_block(dev, mtable[0].bmap, buf);
	clr_bit(buf, bno-1);
	// write buf back
	put_block(dev, mtable[0].bmap, buf);
	// update free inode count in SUPER and GD
	incFreeBlocks(dev);
}

// increments free INODES when an item is removed
void incFreeInodes(int dev){
	char buf[BLKSIZE];// inc free inodes count in SUPER and GD
	SUPER *spTmp;
	GD *gpTmp;

	get_block(dev, 1, buf);
	spTmp = (SUPER *)buf;
	spTmp->s_free_inodes_count++;
	put_block(dev, 1, buf);
	get_block(dev, 2, buf);
	gpTmp = (GD *)buf;
	gpTmp->bg_free_inodes_count++;
	put_block(dev, 2, buf);
}

// incriments Free datablocks when an item is removed
void incFreeBlocks(int dev){
	char buf[BLKSIZE];// inc free inodes count in SUPER and GD
	SUPER *spTmp;
	GD *gpTmp;

	get_block(dev, 1, buf);
	spTmp = (SUPER *)buf;
	spTmp->s_free_blocks_count++;
	put_block(dev, 1, buf);
	get_block(dev, 2, buf);
	gpTmp = (GD *)buf;
	gpTmp->bg_free_blocks_count++;
	put_block(dev, 2, buf);
}

//load INODE of (dev,ino) into a minode[]; return mip->minode[]
MINODE* iget(int dev, int ino)
{
	//search minode[ ] array for an item pointed by mip with the SAME (dev,ino)
	for (int i = 0; i < NMINODE; i++)
	{
		if ((minode[i].refCount != 0) && (minode[i].dev == dev) && (minode[i].ino == ino))
		{
			//it was found in the in memory inode array
			minode[i].refCount++;
			return &minode[i];
		}
	}
	/*if we got down to here it wasnt found in the in-memory inode array, so we need to put it in there then*/

	//search for an open spot in the in memory inode array (the first spot with refCount == 0)
	int i = 0;
	while (i < NMINODE)
	{
		if (minode[i].refCount == 0)
		{
			minode[i].refCount = 1;
			minode[i].dev = dev;
			minode[i].ino = ino;
			minode[i].dirty = 0;
			minode[i].mounted = 0;
			minode[i].mptr = NULL;
			break;
		}
		i++;
	}

	/*
	* now we will use the mailman algorithm to compute:
	*	blk (the block containing THIS inode)
	*	disp (which inode in the above block)
	*/
	int blk = ino / 8;
	int disp = ino % 8;
	if (disp == 0) { blk--; disp = 8; }

	get_block(dev, mtable[0].iblock + blk, minode[i].buf);	//load the block from the given inode into buf starting from the first iblock/
	minode[i].INODE = (INODE *)minode[i].buf; //ip now points at #1 node and we want it to point to the disp inode

	//Searching for the exact inode...
	int j = 1;
	while ((j != disp) && (j < 9))
	{
		minode[i].INODE++;
		j++;
	}

	return &minode[i];
}

//dispose of a minode[] pointed by mip
void iput(MINODE *mip)
{
	INODE * ip2;
	char buf[BLKSIZE];
	mip->refCount--;	//we are done using this inode so de-incriment refCount by one

	if (mip->refCount > 0) { return; }	//someone else is still using it, dont delete it
	if (mip->dirty == 0) { return; }	//inode was never modified, no need to write back to disk

	/* --Now we write the inode back to the disk--

	* now we will use the mailman algorithm to and mip->ino compute:
	*	blk (the block containing THIS inode)
	*	disp (which inode in the above block)
	*/
	int blk = mip->ino / 8;
	int disp = mip->ino % 8;
	if (disp == 0) { blk--; disp = 8; }

	get_block(mip->dev, mtable[0].iblock + blk, buf);	//grab the block containing the inode from the disk
	ip2 = (INODE *)buf;	//point the inode pointer to the buf with the offset of disp to get the exact inode

	int j = 1;
	while ((j != disp) && (j < 9))
	{
		ip2++;
		j++;
	}

	*ip2 = *mip->INODE;         // copy INODE into *ip

	put_block(mip->dev, mtable[0].iblock + blk, buf);
}

//returns the inode number of where 'name' exists in the directory of mip (returns 0 if 'name' does not exist)
int search(MINODE *mip, char *name)
{
	char *cp;	//used for traversing through entries
	char temp[256];		//used to store the name of the current entry
	char buf[BLKSIZE];
	DIR *dp;
	//
	//do indirect blocks first (i_block[0] - i_block[11]
	for (int i = 0; i < 14; i++)
	{
		//only search in non-empty blocks
		if (mip->INODE->i_block[i])
		{
			get_block(mip->dev, mip->INODE->i_block[i], buf);		//get the data from the i_block and put it into buf

			dp = (DIR *)buf;	//creating a DIR struct from buf data block
			cp = buf;

			//search the whole block
			while (cp < buf + BLKSIZE)
			{
				if ((strncmp(name, dp->name, dp->name_len) == 0) && (strlen(name) == dp->name_len)) { return dp->inode; } //it found the inode in the directory matching 'name' so return the value
				
				cp += dp->rec_len; // advance cp to the next entry (by rec_len)
				dp = (DIR *)cp;		//have the dir entry follow along
			}
		}
	}
	/* TODO: also implement indirect blocks (i_block[12]) and double indirect blocks (i_block[13]) */
	return 0;
}

//return the inode number of where the inputted pathname is located
int getino(char *pathname){
	char *pthCpy = malloc(strlen(pathname) * sizeof(char));
	strcpy(pthCpy, pathname);

	int i, n, ino, blk, disp;
	char buf[BLKSIZE];
	char *pch;
	MINODE *mip;
	dev = root->dev; // only ONE device so far

	if (strcmp(pthCpy, "/") == 0) { return 2; }	//if they were looking for root, return inode 2 becuase that is were the root is located
	if (pthCpy[0] == '/') { mip = iget(dev, 2); n = 0; }	//if absolute, start from root, also set number of names (n) to 0
	else { mip = iget(dev, running->cwd->ino); n = 1; }	//if relative, start from current working directory (cwd), also set number of names (n) to 1

	/* Now tokenize the pthCpy */
	i = 0;
	while (pthCpy[i]) { if (pthCpy[i] == '/') { n++; } i++; }	//count how many names there are in the pthCpy

	char **names = malloc((n + 1) * sizeof(char *));	//need to create an array of strings to hold all of the names

	pch = strtok(pthCpy, "/");

	for (i = 0; i < n; i++)
	{
		names[i] = (char *)malloc(strlen(pch) + 1);
		names[i] = pch;
		pch = strtok(NULL, "/");
	}
	names[n + 1] = NULL;

	/* Now get the inode number */

	for (i = 0; i < n; i++)
	{
		ino = search(mip, names[i]);		//get the inode number of the name

		if (ino == 0)
		{
			iput(mip);
			return 0;
		}

		iput(mip);
		mip = iget(dev, ino);	//go to the found inode
	}
	iput(mip);
	return ino;		//return the final found inode number
}

//Initialize the data structires of LEVEL-1 with an inputted device 
void init()
{
	printf("Initalizing procs...");
	//initialize both PROCs, P0 with uid=0, P1 with uid=1, all PROC.cwd = 0:
	for (int i = 0; i < 16; i++){
		proc[0].fd[i] = NULL;
		proc[1].fd[i] = NULL;
	}
	proc[0].uid = 0;
	proc[1].uid = 1;
	proc[0].cwd = NULL;
	proc[1].cwd = NULL;
	printf("Done\n");

	printf("Initalizing in-memory inode table...");
	//initialize minode[100] with all refCount = 0 (show that everything is empty)
	for (int i = 0; i < NMINODE; i++)
	{
		minode[i].refCount = 0;
	}
	printf("Done\n");

	printf("Initalizing root to NULL...");
	root = NULL;	//no root yet
	printf("Done\n");
}

/************************************************************************
*								LEVEL 1
***********************************************************************/

//mounts the disk to the root file system and esteblishes / and CWDs
void mount_root(char *device)
{
	printf("Starting mount procedure...");
	char buf[BLKSIZE];
	printf("Done\n");

	printf("Opening the disk...");
	dev = open(device, O_RDWR);
	if (dev < 0) { printf("open %s failed\n", device); exit(1); }
	printf("Done\n");

	printf("Getting the superblock...");
	get_block(dev, 1, buf); //get superblock
	sp = (SUPER *)buf; //assign into super_block struct
	printf("Done\n");

	printf("Checking if disk is a EXT2 file system...");
	//check to see if device is a EXT2 file system
	if (sp->s_magic != 0xEF53) { printf("NOT an EXT2 file system!\n"); exit(2); }
	printf("Done\n");

	printf("Filling out the memtable with the disk information...");
	mtable[0].dev = dev;
	mtable[0].nblock = sp->s_blocks_count;
	mtable[0].ninodes = sp->s_inodes_count;
	mtable[0].mountDirPtr = root;
	strcpy(mtable[0].deviceName, device);
	strcpy(mtable[0].mountedDirName, "/");
	printf("Done\n");

	printf("Getting the group descriptor block...");
	get_block(dev, 2, buf); //get group descriptor block
	gp = (GD *)buf; //assign into group_desc struct
	printf("Done\n");

	printf("Filling out more memtable information...");
	mtable[0].bmap = gp->bg_block_bitmap;
	mtable[0].iblock = gp->bg_inode_table;
	mtable[0].imap = gp->bg_inode_bitmap;

	//memcpy(&imap, &gp->bg_inode_table, sizeof(int));

	printf("Done\n");

	printf("Assigning root to the second inode...");
	root = iget(dev, 2);	//get and set the root inode
	printf("Done\n");

	printf("Assigning both PROCs cwd to root...");
	//Let cwd of both P0 and P1 point at the root minode
	proc[0].cwd = iget(dev, 2);
	proc[1].cwd = iget(dev, 2);
	printf("Done\n");

	printf("Assigning the running PROC to proc[0]...");
	running = &proc[0];
	printf("Done\n\n");
	printf("---- success mounting %s ----\n", device);

}

// ----------------------------------------- LS
//a helper function to ls_dir and ls where it will ls a file (or dir but not the contents of dir)
//ino is current inode, pino is parent inode
void ls_file(int ino, int pino){
	char *name;	//name of the inode
	int symb = 0;	//symbolic file indicator
	int parent_ino;
	char buf[BLKSIZE];
	int nameLength;
	DIR *dp;
	char *cp;
	MINODE *mip;

	mip = iget(mtable[0].dev, ino);

	char *t1 = "xwrxwrxwr-------";
	char *t2 = "----------------";

	//writing if its dir, reg, or link...
	if ((mip->INODE->i_mode & 0xF000) == 0x8000) { printf("%c", '-'); }
	if ((mip->INODE->i_mode & 0xF000) == 0x4000) { printf("%c", 'd'); }
	if ((mip->INODE->i_mode & 0xF000) == 0xA000) { printf("%c", 'l'); symb = 1; }
	//if ((mip->INODE->i_mode & 0xF000) == 0xA000) { symb = 1; }	//its a symbolic file/

	//writing the permission bits...
	for (int i = 8; i >= 0; i--)
	{
		if (mip->INODE->i_mode & (1 << i)) { printf("%c", t1[i]); }
		else { printf("%c", t2[i]); }
	}

	printf("%4d ", mip->INODE->i_links_count);
	printf("%4d ", mip->INODE->i_gid);
	printf("%4d ", mip->INODE->i_uid);
	printf("%8d ", mip->INODE->i_size);

	//print the time
	char tmeStr[64];
	time_t inodeTime = mip->INODE->i_ctime;
	strcpy(tmeStr, ctime(&inodeTime));
	tmeStr[strlen(tmeStr) - 1] = 0;
	printf("%s  ", tmeStr);

	iput(mip);	//make sure to throw away in-memory inode
	
	mip = iget(mtable[0].dev, pino);	//get in-memory inode of my parent

	/* now try to get my name */
	get_block(mtable[0].dev, mip->INODE->i_block[0], buf);	//load in parent directory into buf
	dp = (DIR *)buf;	//creating a DIR struct from buf data block
	cp = buf;

	//looking for my inode number in my parent dir to get my name...
	while (cp < buf + BLKSIZE)
	{
		if (dp->inode == ino)
		{
			//found my inode number so get my name
			nameLength = (int)dp->name_len;
			for (int i = 0; i < nameLength; i++) { putchar(dp->name[i]); }
			break;
		}
		cp += dp->rec_len; // advance cp to the next entry (by rec_len)
		dp = (DIR *)cp;		//have the dir entry follow along
	}

	/* NOTE: I dont know if we need to print if its a symbolic file or not so ill just leave this template*/
	// print -> linkname if it's a symbolic file
	if (symb)
	{
		mip = iget(mtable[0].dev, ino);
		char curChar;
		int j = -1;
		int i = 0;

		printf(" -> ");

		while (1)
		{
			if (i % 4 == 0) { j++; }

			curChar = (mip->INODE->i_block[j] >> ((i % 4) * 8)) & 0xff;

			if (curChar == '\0') { break; }
			putchar(curChar);

			i++;
		}
	}

	printf("\n");
	iput(mip);	//make sure to throw away in-memory inode at the end
}

//a helper function to ls and will list all files in the directory
void ls_dir(MINODE *mip){
	char buf[BLKSIZE];
	char *cp;	//used for traversing through entries
	DIR *dp;
	int j = 0;

	//iterate through the directory (skipping . and ..)
	for (int i = 0; i < 14; i++)
	{
		get_block(mip->dev, mip->INODE->i_block[i], buf);	//load in current directory into buf
		dp = (DIR *)buf;	//creating a DIR struct from buf data block
		cp = buf;

		if (mip->INODE->i_block[i])
		{
			while (cp < buf + BLKSIZE)
			{
				if (j > 1) { ls_file(dp->inode, mip->ino); }//ls the current file we are on

				cp += dp->rec_len; // advance cp to the next entry (by rec_len)
				dp = (DIR *)cp;	//have the dir entry follow along
				j++;
			}
		}
	}
}

//if a pathname is included, ls the pathname, otherwise ls current working directory
void ls(char *pathname){
	int ino;
	int i = 0;	//current char of pathname
	int j = 0;	//location of last '/'
	MINODE *mip;
	
	if (pathname == NULL)
	{
		//ls the current working directory
		ls_dir(running->cwd);
		return;
	}

	ino = getino(pathname);
	if (ino == 0) { return; }	//the pathname doesnt exist

	mip = iget(mtable[0].dev, ino);

	if ((mip->INODE->i_mode & 0xF000) == 0x4000) { ls_dir(mip); }	//'pathname' is a directory so ls everything inside 
	else
	{
		//if it is not a directory, remove self from pathname string to get the parent inode number
		while (pathname[i])
		{
			if (pathname[i] == '/') { j = i; }
			i++;
		}

		if (j == 0) { ls_file(ino, running->cwd->ino); }
		else
		{
			for (i = j + 1; i < strlen(pathname); i++)
			{
				pathname[i] = '\0';
			}
			ls_file(ino, getino(pathname));
		}
	}

	iput(mip);	//make sure to throw away the in-memory inode
}

// ----------------------------------------- CD
//changes working directory to the inputted pathname, if no pathname is given, will change to root
void cd(char *pathname)
{
	if (pathname == NULL)
	{
		//there was no pathname included so change directory to root
		running->cwd = root;
		return;
	}

	int ino;
	MINODE *mip;

	ino = getino(pathname);
	if (ino == 0) { return; }	//pathname didnt exist

	mip = iget(mtable[0].dev, ino);

	//verify that mip->INODE is a directory...
	if ((mip->INODE->i_mode & 0xF000) != 0x4000) { printf("path %s is not a direcory, cannot cd\n", pathname); iput(mip); return; }

	iput(running->cwd);		//remove old cwd
	running->cwd = mip;		//assign new cwd
}

// ----------------------------------------- PWD
//recursivley prints the current working directory
void rpwd(MINODE *wd){
	if (wd == root) { return; }

	/* search for my current inode number */
	char buf[BLKSIZE];
	char *cp;	//used for traversing through entries
	int parent_ino;
	DIR *dp;
	MINODE *mip;

	get_block(wd->dev, wd->INODE->i_block[0], buf);	//load in current directory into buf
	dp = (DIR *)buf;	//creating a DIR struct from buf data block
	cp = buf;

	//looking for my parents inode number...
	while (cp < buf + BLKSIZE)
	{
		if ((strncmp("..", dp->name, 2) == 0) && (dp->name_len == 2))
		{
			//found my parent inode number
			parent_ino = dp->inode;
			break;
		}
		cp += dp->rec_len; // advance cp to the next entry (by rec_len)
		dp = (DIR *)cp;		//have the dir entry follow along
	}

	/* now try to get my name */
	mip = iget(mtable[0].dev, parent_ino);	//write parent directory into memory
	get_block(mip->dev, mip->INODE->i_block[0], buf);	//load in parent directory into buf
	dp = (DIR *)buf;	//creating a DIR struct from buf data block
	cp = buf;

	char name[256];

	//looking for my inode number to get my name...
	while (cp < buf + BLKSIZE)
	{
		if (dp->inode == wd->ino)
		{
			//found my inode number so get my name
			for (int i = 0; i < dp->name_len; i++) { name[i] = dp->name[i]; }
			name[dp->name_len] = '\0';	//assign NULL terminator to end
			break;
		}
		cp += dp->rec_len; // advance cp to the next entry (by rec_len)
		dp = (DIR *)cp;		//have the dir entry follow along
	}

	rpwd(mip);

	iput(mip);	//remove mip

	printf("/%s", name);
}

//prints current wording directory (calls rpwd as a helper function)
void pwd(MINODE *wd)
{
	if(wd == root){ printf("/"); }
	else { rpwd(wd); }
	printf("\n");
}

// ----------------------------------------- MKDIR
//a helper function to my_mkdir that places the newly-created directory into a dir entry of the parent directory
	//NOTE: currently will fuck up the disk if the device is full so maybe add some handlers for that?
void enter_child(MINODE *pmip, int ino, char *child){
	char *cp;	//used for traversing through entries
	char buf[BLKSIZE];
	bzero(buf, BLKSIZE);
	DIR *dp;
	
	int need_length = 4 * ((8 + strlen(child) + 3) / 4);	//how much space our new directory is going to take up in the parent directory (always has to be a multiple of 4)
	int remain;	//LAST entry's rec_len - its ideal_length
	int lastEntryIdealSize;	//the last entry's ideal size
	int last_rec_len;

	for (int i = 0; i < 12; i++)
	{
		if (!pmip->INODE->i_block[i]) 
		{ 
			//if we got here that means that there wasnt room in the existing blocks so we need to allocate another one to the parent
			char buf2[BLKSIZE];
			bzero(buf2, BLKSIZE); // optional: clear buf[ ] to 0
			DIR *dp2;
			dp2 = (DIR *)buf2;

			int blk = balloc(pmip->dev);	//allocate new data block for parent

			pmip->INODE->i_block[i] = blk;	//assign new data block nnumber to parent
			pmip->INODE->i_size += BLKSIZE;	//since we are adding another data block to the parent we need to increment the size by BLKSIZE

			//make new dir entry
			dp->inode = ino;	//point the new dir entry to the newly created directory
			dp->name_len = strlen(child);	//make the name as long as the user requested
			dp->rec_len = BLKSIZE;	//since this is the last (and first) entry on the block, it will span the entire data block
			for (int j = 0; j < dp->name_len; j++) { dp->name[j] = child[j]; }	//copy in the directory name

			put_block(pmip->dev, pmip->INODE->i_block[i], buf2);	//we are done modifying the entry, now save it to disk
			return;
		}

		get_block(pmip->dev, pmip->INODE->i_block[i], buf);		//get the data from the i_block and put it into buf/

		dp = (DIR *)buf;	//creating a DIR struct from buf data block
		cp = buf;

		//iterate through the whole block of dir_entries
		while (cp + dp->rec_len < buf + BLKSIZE)
		{
			//last_rec_len = dp->rec_len;
			cp += dp->rec_len; // advance cp to the next entry (by rec_len)
			dp = (DIR *)cp;		//have the dir entry follow along
		}

		//dp now points to the last entry in the data block
		lastEntryIdealSize = (4 * ((8 + dp->name_len + 3) / 4));
		remain = dp->rec_len - lastEntryIdealSize;		//this is how much space is left of the particular data block we are on

		//If there is enough space to insert the new directory in this data block, then insert it
		if (remain >= need_length)
		{
			dp->rec_len = lastEntryIdealSize;	//trim the current last directory entry's rec_len to its ideal length
			//dp = (char *)dp + dp->rec_len;	//shift down the rec_len of the last dir entry to create a new dir entry after it
			cp += dp->rec_len; // advance cp to the next entry (by rec_len)
			dp = (DIR *)cp;		//have the dir entry follow along
			dp->inode = ino;	//point the new dir entry to the newly created directory
			dp->name_len = strlen(child);	//make the name as long as the user requested
			dp->rec_len = remain; //last directory on data block will span the rest of the data block
			for (int j = 0; j < dp->name_len; j++) { dp->name[j] = child[j]; }	//copy in the directory name

			put_block(pmip->dev, pmip->INODE->i_block[i], buf);	//we are done modifying the entry, now save it to disk
			return;
		}
	}
}

//a helper function to mkdir which creates the new directory
void my_mkdir(MINODE *pmip, char *child)
{
	//Part 1: Allocate Inode and Disk Block:
	MINODE *mip;
	DIR *dp;
	char *cp;
	char buf[BLKSIZE];
	bzero(buf, BLKSIZE);

	int ino = ialloc(mtable[0].dev);
	int blk = balloc(mtable[0].dev);

	if (!ino) { printf("There are no more free inodes, cannot mkdir\n"); return; }
	if (!blk) { printf("There are no more free data blocks, cannot mkdir\n"); return; }

	//Part 2: Create INODE:
	mip = iget(mtable[0].dev, ino);
	mip->INODE->i_mode = 0x41ED; // 040755: DIR type and permissions
	mip->INODE->i_uid = running->uid; // owner uid
	mip->INODE->i_gid = running->pid; // group Id
	mip->INODE->i_size = BLKSIZE; // size in bytes
	mip->INODE->i_links_count = 2; // links count=2 because of . and ..
	mip->INODE->i_atime = mip->INODE->i_ctime = mip->INODE->i_mtime = time(0);
	mip->INODE->i_blocks = 2; // LINUX: Blocks count in 512-byte chunks
	mip->INODE->i_block[0] = blk; // new DIR has one data block set as the one we allocated for it	
	for (int i = 1; i < 15; i++) { mip->INODE->i_block[i] = 0; }	//new dir only has one data block so set the rest to empty (0)/
	mip->dirty = 1; // mark minode dirty
	iput(mip); // write INODE to disk

	//Part 3: Create data block for new DIR containing . and .. entries:
	dp = (DIR *)buf;
	cp = buf;

	// make "." entry:
	dp->inode = ino;	//allocate . to itself (because dir entry "." is a loopback to itself)
	dp->rec_len = 12;	//size of entry, refer to ideal_length algorithm
	dp->name_len = 1;	//name . is only one char long
	dp->name[0] = '.';	//allocate "." as the name	

	//make ".." entry:
	cp += dp->rec_len; // advance cp to the next entry (by rec_len)
	dp = (DIR *)cp;		//have the dir entry follow along
	//dp = (char *)dp + 12;	//shift down 12 from "." entry (rec_len of ".")
	dp->inode = pmip->ino;	//point ".." to the parent directory (because dir entry ".." points to parent directory)
	dp->rec_len = BLKSIZE - 12; //last directory on data block will span the rest of the data block (subtracting 12 because that is the size of ".")
	dp->name_len = 2;	//name .. is 2 chars long
	dp->name[0] = dp->name[1] = '.';	//set the name of the entry to ".."
	
	put_block(mtable[0].dev, blk, buf); // rewrite updated data block back to the disk

	//Part 4: Enter child into parent directory:
	enter_child(pmip, ino, child);
}

//creates a new directory at 'pathname'
void mkNewDir(char *pathname)
{
	char *parentDir;
	char *child;
	int pino;
	MINODE *pmip;
	
	if (!pathname)
	{
		printf("Must include a name\n");
		return;
	}

	//char *pathnameCpy1 = malloc((strlen(pathname) + 1) * sizeof(char));
	//char *pathnameCpy2 = malloc((strlen(pathname) + 1) * sizeof(char));/

	char *pathnameCpy1;
	char *pathnameCpy2;

	pathnameCpy1 = malloc(strlen(pathname) * sizeof(char));
	pathnameCpy2 = malloc(strlen(pathname) * sizeof(char));

	strcpy(pathnameCpy1, pathname);
	strcpy(pathnameCpy2, pathname);
	//strcpy(parentDir, dirname(pathnameCpy1));
	//strcpy(child, basename(pathnameCpy2));

	parentDir = dirname(pathnameCpy1);
	child = basename(pathnameCpy2);

	//printf("parentDir = %s   child name = %s\n", parentDir, child);

	if ((parentDir == "/") && (child == "/"))
	{
		printf("Must include a name\n");
		return;
	}

	if (getino(pathname)) { printf("%s already exists, cannot mkdir\n", pathname); return; }
	if (!strcmp(parentDir, ".")) { pino = running->cwd->ino; }
	else if (!(pino = getino(parentDir))) { printf("Parent %s does not exist, cannot mkdir\n", parentDir); return; }

	pmip = iget(mtable[0].dev, pino);
	if ((pmip->INODE->i_mode & 0xF000) != 0x4000) { printf("Parent %s is not a direcory, cannot mkdir\n", parentDir); iput(pmip); return; }

	//printf("Parent directory inode number is: %d\n", pino);

	my_mkdir(pmip, child);
	pmip->INODE->i_links_count++;
	pmip->dirty = 1;
	iput(pmip);	

}

// ----------------------------------------- CREAT
void my_creat(MINODE *pmip, char *child) {
	//Part 1: Allocate Inode and Disk Block:
	MINODE *mip;
	DIR *dp;
	char *cp;
	char buf[BLKSIZE];
	bzero(buf, BLKSIZE);

	int ino = ialloc(mtable[0].dev);

	//printf("[my_creat] allocated inode number %d for %s\n", ino, child);

	//int blk = balloc(mtable[0].dev);								// touch

	//printf("allocated data block number: %d and inode number: %d for %s\n", blk, ino, child);

	if (!ino) { printf("There are no more free inodes, cannot creat\n"); return; }
	//if (!blk) { printf("There are no more free data blocks, cannot creat\n"); return; }								// touch

	//Part 2: Create INODE:
	mip = iget(mtable[0].dev, ino);
	mip->INODE->i_mode = 0x81a4; // 040755: DIR type and permissions
	mip->INODE->i_uid = running->uid; // owner uid
	mip->INODE->i_gid = running->pid; // group Id
	mip->INODE->i_size = 0; // size in bytes
	mip->INODE->i_links_count = 1; // links count=2 because of . and ..								// touch
	mip->INODE->i_atime = mip->INODE->i_ctime = mip->INODE->i_mtime = time(0);
	mip->INODE->i_blocks = 0; // LINUX: Blocks count in 512-byte chunks
	mip->INODE->i_block[0] = 0; // new DIR has one data block set as the one we allocated for it	
	for (int i = 1; i < 15; i++) { mip->INODE->i_block[i] = 0; }	//new dir only has one data block so set the rest to empty (0)/
	mip->dirty = 1; // mark minode dirty
	iput(mip); // write INODE to disk

	//Part 3: Create data block for new DIR containing . and .. entries:
	dp = (DIR *)buf;
	cp = buf;


	//Part 4: Enter child into parent directory:
	enter_child(pmip, ino, child);
}

void newCreat(char *pathname) {
	char *parentDir;
	char *child;
	int pino;
	MINODE *pmip;

	if (!pathname)
	{
		printf("Must include a name\n");
		return;
	}

	char *pathnameCpy1;
	char *pathnameCpy2;

	pathnameCpy1 = malloc(strlen(pathname) * sizeof(char));
	pathnameCpy2 = malloc(strlen(pathname) * sizeof(char));

	strcpy(pathnameCpy1, pathname);
	strcpy(pathnameCpy2, pathname);

	parentDir = dirname(pathnameCpy1);
	child = basename(pathnameCpy2);

	//printf("parentDir = %s   child name = %s\n", parentDir, child);

	if ((parentDir == "/") && (child == "/"))
	{
		printf("Must include a name\n");
		return;
	}

	if (getino(pathname)) { printf("%s already exists, cannot creat\n", pathname); return; }
	if (!strcmp(parentDir, ".")) { pino = running->cwd->ino; }
	else if (!(pino = getino(parentDir))) { printf("Parent %s does not exist, cannot creat\n", parentDir); return; }

	pmip = iget(mtable[0].dev, pino);
	if ((pmip->INODE->i_mode & 0xF000) != 0x4000) { printf("Parent %s is not a direcory, cannot creat\n", parentDir); iput(pmip); return; }

	//printf("Parent directory inode number is: %d\n", pino);

	my_creat(pmip, child);
	//pmip->INODE->i_links_count++;
	pmip->dirty = 1;
	iput(pmip);
}

// ----------------------------------------- RMDIR
// checks through mip and returns false if there is anything more than '.' & '..'
int isDir_empty(MINODE *mip){
	char buf[BLKSIZE];
	char *cp;	//used for traversing through entries
	DIR *dp;
	int j = 0;

	//iterate through the directory (skipping . and ..)
	for (int i = 0; i < 12; i++)
	{
		get_block(mip->dev, mip->INODE->i_block[i], buf);	//load in current directory into buf
		dp = (DIR *)buf;	//creating a DIR struct from buf data block
		cp = buf;

		if (mip->INODE->i_block[i])
		{
			while (cp < buf + BLKSIZE)
			{
				cp += dp->rec_len; // advance cp to the next entry (by rec_len)
				dp = (DIR *)cp;	//have the dir entry follow along
				j++;
			}
		}	
	}
	
	if (j > 2) { return 0; }
	else { return 1; }
}

// checks where the child is located then removes keeping contents in tact
int rm_child(MINODE *pmip, char *name){

	// search not necessary b/c name is the item # in the dir
	char buf[BLKSIZE];
	char *cp;	//used for traversing through entries
	char *cpPrev;
	DIR *dp;

	int rmvDirIdealSize;

	//search for my name in parent directory
	for (int i = 0; i < 12; i++)
	{
		//only search in non-empty blocks
		if (pmip->INODE->i_block[i])
		{
			get_block(pmip->dev, pmip->INODE->i_block[i], buf);		//get the data from the i_block and put it into buf

			dp = (DIR *)buf;	//creating a DIR struct from buf data block
			cp = buf;

			//search the whole block
			while (cp < buf + BLKSIZE)
			{
				if ((strncmp(name, dp->name, dp->name_len) == 0) && (strlen(name) == dp->name_len)) 
				{ 
					//it found the inode in the directory matching 'name'
					rmvDirIdealSize = (4 * ((8 + dp->name_len + 3) / 4));

					if (dp->rec_len == BLKSIZE)
					{
						//it is the only directory on the data block
						char buf2[BLKSIZE];
						bzero(buf2, BLKSIZE);
						bdalloc(pmip->dev, dp->inode);	//deallocating data block	
						pmip->INODE->i_block[i] = 0;

						pmip->INODE->i_size -= BLKSIZE;	//since we are removing a data block from the parent we need to de-increment the size by BLKSIZE

						//if there are more data blockd being used by the parent, shift them over
						int j = i;
						while (pmip->INODE->i_block[j + 1])
						{
							pmip->INODE->i_block[j] = pmip->INODE->i_block[j + 1];
							pmip->INODE->i_block[j + 1] = 0;
						}

						return 1;
					}

					if ((dp->rec_len > rmvDirIdealSize) || (cp + dp->rec_len == buf + BLKSIZE))
					{
						// it is the last direcotry on the data block (but not only one on the block)
						
						int rmDirRec_len = dp->rec_len;
						dp = (DIR *)cpPrev;	//go to the entry before me
						dp->rec_len += rmDirRec_len;	//absorb the rmdir rec_len into the previous entry (now last)
						put_block(pmip->dev, pmip->INODE->i_block[i], buf);
						return 1;
					}

					if (dp->rec_len == rmvDirIdealSize)
					{
						//it is in the middle or it is the first (but not only one on the block)
						char *cpSft;	//used for traversing through entries
						DIR *dpSft;
						DIR dirCpy;

						cpSft = cp + rmvDirIdealSize;
						dpSft = (DIR *)buf;
						dpSft = (DIR *)cpSft;

						int nextRecLen;
						int nextIdealSize;

						while (1)
						{
							nextRecLen = dpSft->rec_len;
							nextIdealSize = (4 * ((8 + dpSft->name_len + 3) / 4));

							memcpy(dp, cpSft, dpSft->rec_len);

							if (nextRecLen > nextIdealSize)
							{
								//the one after the current entry being copied is the last entry
								dp->rec_len += rmvDirIdealSize;	//add the deleted space to the end of last entry
								break;
							}

							cpSft += dpSft->rec_len; // shift over one
							dpSft = (DIR *)cpSft;

							cp += dp->rec_len;
							dp = (DIR *)cp;
						}

						put_block(pmip->dev, pmip->INODE->i_block[i], buf);
						return 1;
					}
					 
				} 
				cpPrev = cp;
				cp += dp->rec_len; // advance cp to the next entry (by rec_len)
				dp = (DIR *)cp;		//have the dir entry follow along
			}
		}
	}


	return 0;
}

void newRmdir(char *pathname){
	int pino, ino;
	MINODE *pmip;
	MINODE *mip;

	char name[256];

	char buf[BLKSIZE];
	char *cp;	//used for traversing through entries
	DIR *dp;

	char *parentDir;
	char *child;
	char *pathnameCpy1;
	char *pathnameCpy2;

	if (!pathname) { printf("Must include a name\n"); return; }

	pathnameCpy1 = malloc(strlen(pathname) * sizeof(char));
	pathnameCpy2 = malloc(strlen(pathname) * sizeof(char));

	strcpy(pathnameCpy1, pathname);
	strcpy(pathnameCpy2, pathname);

	parentDir = dirname(pathnameCpy1);
	child = basename(pathnameCpy2);

	if ((parentDir == "/") && (child == "/"))
	{
		printf("Must include a name\n");
		return;
	}

	// 1 /* Get in-memory INODE of Pathname */

	if (!(ino = getino(pathname))) { printf("%s does not exist, cannot rmdir\n", pathname); return; }

	//ino = getino(pathname);
	mip = iget(mtable[0].dev, ino);
	
	if ((mip->INODE->i_mode & 0xF000) != 0x4000) { printf("%s is not a directory, cannot remove\n", pathname); iput(mip); return;}

	// 2	/* Verify INODE is a DIR by (INODE.i_mode filed)*/
	if (mip->refCount > 1) { printf("%s is Busy and cannot be deleted\n", pathname); return;}
	if (!isDir_empty(mip)){ printf("%s is not an empty directory, cannot remove\n", pathname); iput(mip); return;}

	// 3	/* Get parents ino and inode */
	if (!strcmp(parentDir, ".")) { pino = running->cwd->ino; }
	else if (!(pino = getino(parentDir))) { printf("Parent %s does not exist, cannot mkdir\n", parentDir); iput(mip); return; }
	else{ pino = getino(parentDir); }
	pmip = iget(mip->dev,pino);

	// 4	/* Get name from parent DIR's data blcok */	
	for (int i = 0; i < 12; i++)
	{
		get_block(pmip->dev, pmip->INODE->i_block[i], buf);	//load in current directory into buf
		dp = (DIR *)buf;	//creating a DIR struct from buf data block
		cp = buf;

		if (pmip->INODE->i_block[i])
		{
			//looking for my inode number to get my name...
			while (cp < buf + BLKSIZE)
			{
				if (dp->inode == ino)
				{
					//found my inode number so get my name
					for (int j = 0; j < dp->name_len; j++) { name[j] = dp->name[j]; }
					name[dp->name_len] = '\0';	//assign NULL terminator to end
					break;
				}
				cp += dp->rec_len; // advance cp to the next entry (by rec_len)
				dp = (DIR *)cp;		//have the dir entry follow along
			}
		}
	}

	// 5	/*remove name from parent directory */
	rm_child(pmip, name);								// *********** !! I Need Help with my RM_Child !! *********
	
	// 6	/* Deallocate its data blocks and inode */
	bdalloc(mip->dev, mip->INODE->i_block[0]);
	idalloc(mip->dev, mip->ino);
	iput(mip);
	
	// 7 	/* Dec parent links_count by 1 : mark parent pimp dirty */
	pmip->INODE->i_links_count--;
	pmip->dirty = 1;
	iput(pmip);
	
}

// ----------------------------------------- LINK
//link old_file new_file
void myLink(char *line) {
	char *old_file;
	char *new_file;
	char *pathnames;

	strtok(line, " ");

	int oino;
	int pino;
	MINODE *omip;
	MINODE *pmip;

	old_file = strtok(NULL, " ");
	new_file = strtok(NULL, " ");

	//printf("old_file = %s\n", old_file);
	//printf("new_file = %s\n", new_file);

	char *parentDir;
	char *child;
	char *pathnameCpy1;
	char *pathnameCpy2;

	if (!new_file) { printf("Must include a name\n"); return; }

	pathnameCpy1 = malloc(strlen(new_file) * sizeof(char));
	pathnameCpy2 = malloc(strlen(new_file) * sizeof(char));

	strcpy(pathnameCpy1, new_file);
	strcpy(pathnameCpy2, new_file);

	parentDir = dirname(pathnameCpy1);
	child = basename(pathnameCpy2);

	
	if ((oino = getino(old_file)) == 0) { printf("%s does not exist, cannot link\n", old_file); return; }

	omip = iget(mtable[0].dev, oino);
	if ((omip->INODE->i_mode & 0xF000) == 0x4000) { printf("%s is a directory, cannot link\n", old_file); iput(omip); return; }

	if ((pino = getino(parentDir)) == 0) { printf("%s does not exist, cannot link\n", parentDir); iput(omip); return; }

	pmip = iget(mtable[0].dev, pino);
	if ((pmip->INODE->i_mode & 0xF000) != 0x4000) { printf("%s is not a directory, cannot link\n", parentDir); iput(pmip); iput(omip); return; }

	if (~(search(pmip, child)))
	{
		enter_child(pmip, oino, child);
		omip->INODE->i_links_count++;
		omip->dirty = 1;
	}
	
	printf("Sucessfully linked %s -> %s\n",old_file, new_file);

	iput(omip);
	iput(pmip);
}

// ----------------------------------------- SYMLINK
//link old_file new_file
void mySyslink(char *line){
	char *old_file;
	char *new_file;
	char *pathnames;

	strtok(line, " ");

	int oino;
	int pino;
	int ino;

	MINODE *omip;
	MINODE *pmip;
	MINODE *mip;

	old_file = strtok(NULL, " ");
	new_file = strtok(NULL, " ");

	if ((oino = getino(old_file)) == 0) { printf("%s does not exist, cannot syslink\n", old_file); return; }
	if ((ino = getino(new_file)) != 0) { printf("%s already exists, cannot syslink\n", new_file); return; }

	newCreat(new_file);
	if ((ino = getino(new_file)) == 0) { return; }	//return if it couldnt make a new file

	mip = iget(mtable[0].dev, ino);
	mip->INODE->i_mode = (mip->INODE->i_mode & 0x0FFF) | 0xA000; //change to LNK type
	mip->INODE->i_size = strlen(old_file); //size is the name length of old file

	int j = -1;
	for (int i = 0; i < strlen(old_file); i++)
	{
		if (i % 4 == 0) { j++; }
		mip->INODE->i_block[j] |= (old_file[i] << (i % 4) * 8);
	}

	printf("Sucessfully linked %s -> %s\n",old_file, new_file);
	mip->dirty = 1;
	iput(mip);
}

// ------------------------------------------ UNLINK
void myUnlink(char *inPath)
{
	char *pathname = malloc(strlen(inPath) * sizeof(char));
	strcpy(pathname, inPath);
	int ino;
	int pino;
	MINODE *mip;
	MINODE *pmip;

	char *parentDir;
	char *child;
	char *pathnameCpy1;
	char *pathnameCpy2;

	if (!pathname) { printf("Must include a name\n"); return; }

	pathnameCpy1 = malloc(strlen(pathname) * sizeof(char));
	pathnameCpy2 = malloc(strlen(pathname) * sizeof(char));

	strcpy(pathnameCpy1, pathname);
	strcpy(pathnameCpy2, pathname);

	parentDir = dirname(pathnameCpy1);
	child = basename(pathnameCpy2);

	if ((ino = getino(pathname)) == 0) { printf("%s does not exist, cannot unlink\n", pathname); return; }

	mip = iget(mtable[0].dev, ino);
	if ((mip->INODE->i_mode & 0xF000) == 0x4000) { printf("%s is a directory, cannot unlink\n", pathname); iput(mip); return; }

	pino = getino(parentDir);
	pmip = iget(mtable[0].dev, pino);

	rm_child(pmip, child);		//remove it from parent

	pmip->dirty = 1;
	iput(pmip);

	mip->INODE->i_links_count--;

	if (mip->INODE->i_links_count > 0) { mip->dirty = 1; }	//if links_count > 0 then its still being referenced somewhere so dont delete it
	else
	{
		for (int i = 0; i < 14; i++)
		{
			if (mip->INODE->i_block[i])
			{
				bdalloc(mip->dev, mip->INODE->i_block[i]);	//deallocate all data blocks associated with the inode
			}
		}
		idalloc(mip->dev, ino);		//deallocate the inode
	}
	iput(mip);
}

// ----------------------------------------- STAT
void myStat(char *pathname)
{
	int ino;
	MINODE *mip;

	if (!pathname) { printf("Must include a name\n"); return; }
	if (!(ino = getino(pathname))) { printf("%s does not exist, cannot stat\n", pathname); return; }
	mip = iget(mtable[0].dev, ino);

	char tmeStr[64];
	time_t inodeTime = mip->INODE->i_atime;
	strcpy(tmeStr, ctime(&inodeTime));
	tmeStr[strlen(tmeStr) - 1] = 0;

	printf("********* stat *********\n");
	printf("dev=%d  ", mip->dev);
	printf("ino=%d  ", ino);
	printf("mod=%x\n", mip->INODE->i_mode);
	printf("uid=%d  ", mip->INODE->i_uid);
	printf("gid=%d  ", mip->INODE->i_gid);
	printf("nlink=%d\n", mip->INODE->i_links_count);
	printf("size=%d  ", mip->INODE->i_size);
	printf("time=%s\n", tmeStr);
	printf("************************\n");
}

// ----------------------------------------- TOUCH
void touch(char *pathname){

	if (pathname == NULL){	printf("Error: No Filename given\n");	return;	}

	int ino;
	MINODE *mip;

	ino = getino(pathname);
	if (ino == 0){	printf("Error: File does not Exist\n");	return;	}
	
	mip = iget(mtable[0].dev, ino);
	mip->INODE->i_atime = time(0);

	mip->dirty = 1;
	iput(mip);
}
// ------------------ ----------------------- CHMOD
void my_chmod(char *line){
	char *Mode;
	char *pathname;

	strtok(line, " ");
	pathname = strtok(NULL, " ");
	Mode = strtok(NULL, " ");
	
	if (pathname == NULL){	printf("Error: No Filename given\n");	return;	}

	if (Mode == NULL){
		Mode = "200";
	}

	int i;
	sscanf(Mode, "%o", &i);
	int ino;
	ino = getino(pathname);

	if (ino == 0){	printf("Error: File does not Exist\n");	return;	}
	
	MINODE *mip;
	mip = iget(mtable[0].dev, ino);
	mip->INODE->i_mode = (mip->INODE->i_mode & 0xFE00) | i;

	mip->dirty = 1;
	iput(mip);
	
}


/************************************************************************
*								LEVEL 2
*************************************************************************/
int get_fd(int ino){
	int j = -1;
	for (int i = 0; i < 16; i++){
		if (running->fd[i] == NULL && j == -1){
			j = i;
		}
		if(running->fd[i] != NULL && running->fd[i]->mptr->ino == ino){
			return i;
		}
	}
	return j;
}
// ----------------------------------------- OPEN
// R | W | RW | Append
// 0 | 1 | 2  | 3
int my_open(char  *filename, int flags){
	// 1. Get file minode
	if (filename == NULL){	printf("Error Opening File: Bad File Name\n");	return -1;	}

	int ino;
	MINODE *mip;
	ino = getino(filename);
	
	if (ino == 0){
		newCreat(filename);
		ino = getino(filename);
	}

	// 2. check file permissions
	mip = iget(mtable[0].dev, ino);
	if ((mip->INODE->i_mode & 0xF000) != 0x8000) { printf("Error: Object is not a File, cannot open"); iput(mip);return -1; }

	// 3. allocate an opentable entry 
	OFT *openTable = malloc(sizeof(OFT));

	openTable->mode = flags;
	if (flags == 3){
		//printf("[open] inode size = %d\n", mip->INODE->i_size);
		openTable->offset = mip->INODE->i_size;
		//printf("[open] file descriptor offset = %d\n", openTable->offset);
	}
	else {
		openTable->offset = 0;
	}

	openTable->refCount = 1;
	openTable->mptr = mip;

	// 4. Search for free FD
	int fd = get_fd(ino); 
	if( fd == -1){ printf("Error Opening File: No free openTable slot\n");iput(mip); return -1;}
	running->fd[fd] = openTable;

	// 5. unlock minode and return fd as a file descriptor
	//fd->mptr->INODE->lock = 0;	// this is how this should work but we dont have a lock member in INODE like in his diagrams
	return fd;
}
void mySys_open(char *line){

	char *lineCpy = malloc(strlen(line) * sizeof(char));
	strcpy(lineCpy, line);

	char *filepath;
	int flag;

	strtok(lineCpy, " ");
	filepath = strtok(NULL, " ");
	char *temp = strtok(NULL, " ");
	
	flag = atoi(temp);
	//sscanf(lineCpy,"%d", flag);

	int fd = my_open(filepath, flag);
	printf(">Opend FileDescriptor: %d\n", fd);
}

// ----------------------------------------- CLOSE
int my_close(int fd){
	// 1. check fd is a valid opened file descriptor;
	if(running->fd[fd] == NULL){	printf("Error Closing File: file descriptor\n"); return -1;	}

	// 2. if (PROC's fd[fd] != 0) 		// redundant check
	// 3. opentable check for RW pipe 	// dont need this

	OFT *openTable = (running->fd[fd]);
	openTable->refCount-=1;

	// 4. if last process using this OFT
	if (openTable->refCount == 0){
		iput(openTable->mptr);
		// 5. clear fd[fd]
		running->fd[fd] = NULL;	
	}

	// 6. return SUCCESS
	return 1;
}
// syscall handeler for close
void mySys_close(char *filename){
	int fd = atoi(filename);
	printf("> Closed FileDescriptor: %d\n",fd);
	my_close(fd);
}

// ----------------------------------------- LSEEK
int my_lseek(int fd, int position){

	if ((running->fd[fd]->mode == 0) && (position > running->fd[fd]->mptr->INODE->i_size)){return -1;}

	running->fd[fd]->offset = position;
	return 1;
}
//syscall handeler for lseek
void mySys_lseek(char *line){

	char *lineCpy = malloc(strlen(line) * sizeof(char));
	strcpy(lineCpy, line);

	char *s_fd;
	char *s_position;

	int fd;
	int position;

	strtok(lineCpy, " ");
	s_fd = strtok(NULL, " ");
	s_position = strtok(NULL, " ");
	
	fd = atoi(s_fd);
	position = atoi(s_position);

	my_lseek(fd, position);
}

// ----------------------------------------- READ
//converts the logical block number to the physical block number
int lb_2_pb(MINODE *mip, int lbk) {
	if (lbk < 12) { //the logical block was a direct block
		if (mip->INODE->i_block[lbk] == 0) {
			mip->INODE->i_block[lbk] = balloc(mtable[0].dev);
		}
		return mip->INODE->i_block[lbk]; 
	}	
	else if (12 <= lbk < 12 + 256) {	//the logical block was an indirect block

	}
	else {	//the logical block was a double indirect block

	}
	printf("[lb_2_pb] Could not determine physical block number, aborting now\n");
	return -1;
}

int my_read(int fd, char *buf, int nbytes) {
	int count = 0;	//number of bytes read
	int avil;	//bytes avalible in file
	int lbk;	//logical block of where file is
	int blk;	//the physical block number of where the file is
	int startByte;	//the starting byte of the file within the logical block
	char tmpbuf[BLKSIZE];		//temp storage for copying
	int copied = 0;	//number of bytes copied
	int endIndex;

	if (running->fd[fd] == NULL) {
		printf("[my_read] file descriptor invalid, cannot continue\n");
		return -1;
	}

	//Only continue if the file opened had flags for R or RW:
	if (!(running->fd[fd]->mode == 0 || running->fd[fd]->mode == 2)) {
		printf("[my_read] file descriptor incorrect mode, cannot continue\n");
		return -1;
	}

	avil = running->fd[fd]->mptr->INODE->i_size - running->fd[fd]->offset;	//compute amout of bytes left in file

	//read until it has read all of the bytes that the user requested or until the entire file has been read
	while (0 < nbytes || 0 < avil) {
		lbk = running->fd[fd]->offset / BLKSIZE;		//calculates the logical block
		startByte = (running->fd[fd]->offset % BLKSIZE);	//calculates the start bit

		//if ((blk = lb_2_pb(running->fd[fd]->mptr->INODE, lbk)) == -1) { return; }	//get physical block number
		if ((blk = lb_2_pb(running->fd[fd]->mptr, lbk)) == -1) { return; }
		get_block(running->fd[fd]->mptr->dev, blk, tmpbuf);		//get the block data of file

		//determine the last byte to be copied from the current block:
		if (startByte + nbytes <= BLKSIZE) {
			endIndex = startByte + nbytes;
		}
		else { endIndex = BLKSIZE; }

		if (copied == 0) {	//its the first time copying, so use strcpy
			strncpy(buf, tmpbuf + startByte, endIndex - startByte);
		}
		else {	//it isnt the first time copying, so use strcat
			strncat(buf, tmpbuf + startByte, endIndex - startByte);
		}

		copied = endIndex - startByte;	//calculate how many bytes were just copied
		running->fd[fd]->offset += copied;
		count += copied;
		avil -= copied;
		nbytes -= copied;
	}

	return count;
}

// ----------------------------------------- WRITE 
int my_write(int fd, char *buf, int nbytes) {
	int count = 0;	//number of bytes read
	int avil;	//bytes avalible in file
	int lbk;	//logical block of where file is
	int blk;	//the physical block number of where the file is
	int startByte;	//the starting byte of the file within the logical block
	char tmpbuf[BLKSIZE];		//temp storage for copying
	int copied = 0;	//number of bytes copied
	int endIndex;

	if (running->fd[fd] == NULL) {
		printf("[my_write] file descriptor invalid, cannot continue\n");
		return -1;
	}

	//Only continue if the file opened had flags for W, RW, or APPEND:
	if (running->fd[fd]->mode == 0) {
		printf("[my_write] file descriptor incorrect mode, cannot continue\n");
		return -1;
	}

	//printf("[write] old file size is %d\n", running->fd[fd]->mptr->INODE->i_size);

	//if the file is bigger after writing to it, update file size
	if (running->fd[fd]->mptr->INODE->i_size < running->fd[fd]->offset + nbytes) {
		running->fd[fd]->mptr->INODE->i_size += running->fd[fd]->offset + nbytes;
	}

	//printf("[write] new file size is %d\n", running->fd[fd]->mptr->INODE->i_size);

	avil = strlen(buf);	//compute amout of bytes left in file

	//read until it has read all of the bytes that the user requested or until the entire file has been read
	while (0 < nbytes || 0 < avil) {
		lbk = running->fd[fd]->offset / BLKSIZE;		//calculates the logical block
		startByte = (running->fd[fd]->offset % BLKSIZE);	//calculates the start bit

		//printf("[write] byte offset is %d\n", running->fd[fd]->offset);

		//if ((blk = lb_2_pb(running->fd[fd]->mptr->INODE, lbk)) == -1) { return; }	//get physical block number
		if ((blk = lb_2_pb(running->fd[fd]->mptr, lbk)) == -1) { return; }
		//printf("[write] writing to data block number %d\n", blk);
		get_block(running->fd[fd]->mptr->dev, blk, tmpbuf);		//get the block data of file
		
		if (startByte + nbytes <= BLKSIZE) {
			endIndex = startByte + nbytes;
		}
		else { endIndex = BLKSIZE; }

		if (copied == 0) {	//its the first time copying, so use strcpy
			strncpy(tmpbuf + startByte, buf, endIndex - startByte);
		}
		else {	//it isnt the first time copying, so use strcat
			strncat(tmpbuf + startByte, buf, endIndex - startByte);
		}

		copied = endIndex - startByte;	//calculate how many bytes were just copied
		running->fd[fd]->offset += copied;
		count += copied;
		nbytes -= copied;
		avil -= copied;
		put_block(running->fd[fd]->mptr->dev, blk, tmpbuf);
	}
	running->fd[fd]->mptr->dirty = 1;

	return count;
}
// syscall handeler for write
void mySys_write(char *pathname){
	int fd = atoi(pathname);
	if(running->fd[fd == NULL]){	printf("Error: File Descriptor isnt linked to a file"); return;}
	char line[256];
	printf("=================== Text to Write =====================\n");
	fgets(line, 256, stdin);
	my_write(fd, line, strlen(line));

}

// ----------------------------------------- CAT
void my_cat(char *filename){
	// check given filename
	if (filename == NULL){	printf("Error: No Filename given\n");	return;	}

	// check if file exists
	int ino;
	ino = getino(filename);
	if (ino == 0){	printf("Error: File does not Exist\n");	return;	}
	
	// open file
	int fd = my_open(filename, 0);	// open filename to read=0
	if (fd != -1){
		char *buf = malloc(running->fd[fd]->mptr->INODE->i_size+1);		// if we have errors, check here first lol
		// Read fd[size] into buf[fd.size+1]
		my_read(fd,buf,running->fd[fd]->mptr->INODE->i_size);
		printf("%s", buf);
		my_close(fd);
	}
}

// ----------------------------------------- CP
void my_cp(char *line){
	// check if path is dir 			//TODO
	char *srcPath;
	char *destPath;

	strtok(line, " ");
	srcPath = strtok(NULL, " ");
	destPath = strtok(NULL, " ");
	
	if (srcPath == NULL){	printf("Error: No Source given\n");	return;	}
	if (destPath == NULL){	printf("Error: No Destination given\n");	return;	}

	int src_ino = getino(srcPath);
	if (src_ino == 0){	printf("Error: Source File does not Exist\n");	return;	}
	MINODE *src_mip = iget(mtable[0].dev, src_ino);

	char *destCpy = malloc(strlen(destPath) * sizeof(char));
	strcpy(destCpy, destPath);

	newCreat(destPath);
	int dest_ino;
	if (( dest_ino = getino(destCpy)) == 0) { return; }	//return if it couldnt make a new file
	MINODE *dest_mip = iget(mtable[0].dev, dest_ino);
	
	dest_mip->INODE->i_size = src_mip->INODE->i_size;
	char buf[BLKSIZE];
	for (int i = 0; i < 14; i++){
		//only search in non-empty blocks
		if (src_mip->INODE->i_block[i]){
			dest_mip->INODE->i_block[i] = balloc(mtable[0].dev);
			dest_mip->INODE->i_blocks+=2;
			get_block(mtable[0].dev, src_mip->INODE->i_block[i], buf);
			put_block(mtable[0].dev, dest_mip->INODE->i_block[i], buf);
		}
	}
	dest_mip->dirty = 1;
	iput(dest_mip);
	iput(src_mip);
	
}

// ----------------------------------------- MV
void my_mv(char *line){

	char *lineCpy = malloc(strlen(line) * sizeof(char));
	strcpy(lineCpy, line);

	my_cp(lineCpy);

	char *srcPath;
	strtok(line, " ");
	srcPath = strtok(NULL, " ");

	myUnlink(srcPath);
	  
}

// ----------------------------------------- QUIT
void quit()
{
	//save all minodes with refCount > 0 and is DIRTY
	for (int i = 0; i < NMINODE; i++)
	{
		while ((minode[i].refCount > 0) && minode[i].dirty) { iput(&minode[i]); }	//use while because refCount may be greater than 1
	}
}

/************************************************************************
 *									Main
************************************************************************/
int main(int argc, char *argv[])
{
	char line[256];
	char *lineCpy;
	char *command;
	char *pathname;

	init();
	if (argc > 1) { mount_root(argv[1]); }
	else { mount_root("mydisk"); }
	//else { mount_root("vdisk"); }
	ls(NULL);


	while (1)
	{
		//print input line and displaying CWD
		printf("User@%s:~",argv[1]);
		rpwd(running->cwd);
		printf("$ ");

		// gets userinput from terminal
		bzero(line, 256);                // zero out line[ ]
		fgets(line, 256, stdin);         // get a line (end with \n) from stdin
		line[strlen(line) - 1] = '\0';        // kill \n at end

		lineCpy = malloc(strlen(line) * sizeof(char)); // deep copy of the input string for safety
		strcpy(lineCpy, line);

		// seperating string commands from input parameters
		command = strtok(line, " ");
		pathname = strtok(NULL, " ");

		if (command == NULL) { printf("no input detected!\n"); continue; }
		if((strcmp(command, "ls")) 
		&& (strcmp(command, "cd")) 
		&& (strcmp(command, "pwd")) 
		&& (strcmp(command, "mkdir")) 
		&& (strcmp(command, "creat"))
		&& (strcmp(command, "rmdir")) 
		&& (strcmp(command, "link")) 
		&& (strcmp(command, "unlink"))
		&& (strcmp(command, "symlink")) 
		&& (strcmp(command, "touch"))
		&& (strcmp(command, "chmod"))
		&& (strcmp(command, "stat"))
		&& (strcmp(command, "cat"))
		&& (strcmp(command, "cp"))
		&& (strcmp(command, "mv"))
		&& (strcmp(command, "open"))
		&& (strcmp(command, "close"))
		&& (strcmp(command, "lseek"))
		&& (strcmp(command, "write"))
		&& (strcmp(command, "quit"))){ printf("unknown command!\n"); continue; }


		if (!strcmp(command, "ls")) { ls(pathname); continue; }
		if (!strcmp(command, "cd")) { cd(pathname); continue; }
		if (!strcmp(command, "pwd")) { pwd(running->cwd); continue; }
		if (!strcmp(command, "mkdir")) { mkNewDir(pathname); continue; }
		if (!strcmp(command, "creat")) { newCreat(pathname); continue; }
		if (!strcmp(command, "rmdir")) { newRmdir(pathname); continue; }
		if (!strcmp(command, "link")) { myLink(lineCpy); continue; }
		if (!strcmp(command, "unlink")) { myUnlink(pathname); continue; }
		if (!strcmp(command, "symlink")) { mySyslink(lineCpy); continue; }
		if (!strcmp(command, "touch")) { touch(pathname); continue; }
		if (!strcmp(command, "chmod")) { my_chmod(lineCpy); continue; }
		if (!strcmp(command, "stat")) { myStat(pathname); continue; }
		if (!strcmp(command, "cat")) { my_cat(pathname); continue; }
		if (!strcmp(command, "cp")) { my_cp(lineCpy); continue; }
		if (!strcmp(command, "mv")) { my_mv(lineCpy); continue; }
		if (!strcmp(command, "open")) { mySys_open(lineCpy); continue; }
		if (!strcmp(command, "close")) { mySys_close(pathname); continue; }
		if (!strcmp(command, "lseek")) { mySys_lseek(lineCpy); continue; }
		if (!strcmp(command, "write")) { mySys_write(pathname); continue; }
		if (!strcmp(command, "quit")) { quit(); return 1; }
	}
	return 1;
}