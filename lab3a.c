#define _LARGEFILE64_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <inttypes.h>

int inodeCount;
int blockCount;
int blockSize;
int fragmentSize;
int blocksPerGroup; 
int inodesPerGroup;
int fragmentsPerGroup;
int firstDataBlock;

int numGroups;

int bytesPerInode;

/*
int * containedBlockCount;
int * freeBlockCount;
int * freeInodeCount;
int * directoryCount;
*/

struct groupDescriptorFields {
	int containedBlockCount;
	int freeBlockCount;
	int freeInodeCount;
	int directoryCount;
	char* inodeBitmapBlock;
	char* blockBitmapBlock;
	char* inodeTableBlock;
};

struct groupDescriptorFields* groupDescriptors;

//A list of the inode numbers of allocated inodes
unsigned long* listOfAllocatedInodes;
unsigned long allocatedInodeCount;

//A list of the inode numbers that point to directories
unsigned long* listOfDirectoryInodes;
unsigned long directoryInodeCount;

//A list of the inode numbers that contain single, double, and triple indirect pointers
unsigned long* listOfIndirectInodes;
unsigned long indirectInodeCount;


//Since the file format is in a little endian format, it is useful to turn those bytes into 
//a big endian format (expected by the csv) procedurally. This function does this.
ssize_t preadLittleEndian(int fd, unsigned char* buffer, size_t count, off_t offset) {
	int readCount = pread(fd, buffer, count, offset);
	for (int i = 0; i < readCount / 2; i++) {
		unsigned char temp = buffer[i];
		buffer[i] = buffer[readCount - i - 1];
		buffer[readCount - i - 1] = temp;
	}
	return readCount;
}

//Returns the corresponding integer from a big-endian buffer containing the raw bytes
unsigned int getIntFromBuffer(unsigned char* buffer, size_t count) {
	unsigned int returnInt = 0;
	for (int i = 0; i < count; i++) {
		returnInt = (returnInt << 8) + buffer[i];
	}
	return returnInt;
}

void readSuperBlock(int fd) {
	FILE* writeFileStream = fopen("super.csv", "w+");

	int superBlockOffset = 1024;

	//Read magic number
	unsigned char* buffer = malloc(64); 
	preadLittleEndian(fd, buffer, 2, superBlockOffset + 56); 
		//1080 is where the magic number resides
		//no error checking
	buffer[2] = '\0';

	fprintf(writeFileStream, "%02x%02x,", 
		buffer[0], buffer[1]);

	//Read and store total number of inodes
	preadLittleEndian(fd, buffer, 4, superBlockOffset); //Offset for inode count
	inodeCount = getIntFromBuffer(buffer, 4);
	fprintf(writeFileStream, "%d,", inodeCount);

	//Read and store total number of blocks
	preadLittleEndian(fd, buffer, 4, superBlockOffset + 4); //Offset for block count
	blockCount = getIntFromBuffer(buffer, 4);
	fprintf(writeFileStream, "%d,", blockCount);

	//Read and store block size
	preadLittleEndian(fd, buffer, 4, superBlockOffset + 24); //Offset for block size
	blockSize = 1024 << getIntFromBuffer(buffer, 4);
	fprintf(writeFileStream, "%d,", blockSize);

	//Read and store fragment size
	preadLittleEndian(fd, buffer, 4, superBlockOffset + 28); //Offset for fragment size
	fragmentSize = getIntFromBuffer(buffer, 4);
	if (fragmentSize > 0) {
		fragmentSize = 1024 << fragmentSize;
	}
	else fragmentSize = 1024 >> -1 * fragmentSize;
	fprintf(writeFileStream, "%d,", fragmentSize);

	//Read and store blocks per group
	preadLittleEndian(fd, buffer, 4, superBlockOffset + 32); //Offset for blocks per group
	blocksPerGroup = getIntFromBuffer(buffer, 4);
	fprintf(writeFileStream, "%d,", blocksPerGroup);

	//Read and store inodes per group
	preadLittleEndian(fd, buffer, 4, superBlockOffset + 40); //Offset for inodes per group
	inodesPerGroup = getIntFromBuffer(buffer, 4);
	fprintf(writeFileStream, "%d,", inodesPerGroup);

	//Read and store fragments per group
	preadLittleEndian(fd, buffer, 4, superBlockOffset + 36); //Offset for fragments per group
	fragmentsPerGroup = getIntFromBuffer(buffer, 4);
	fprintf(writeFileStream, "%d,", fragmentsPerGroup);

	//Read and store first data block, as well as newline
	preadLittleEndian(fd, buffer, 4, superBlockOffset + 20); //Offset for fist data block
	firstDataBlock = getIntFromBuffer(buffer, 4);
	fprintf(writeFileStream, "%d\n", firstDataBlock);

	//Get the inode size: needed later
	preadLittleEndian(fd, buffer, 2, superBlockOffset + 88); //Offset for bytes per inode
	bytesPerInode = getIntFromBuffer(buffer, 2);

	fflush(writeFileStream);
}

void readGroupDescriptor(int fd) {
	FILE* writeFileStream = fopen("group.csv", "w+");

	unsigned char* buffer = malloc(16); 

	//offset for group descriptor
	int startGroupDescriptor = (firstDataBlock + 1) * blockSize;

	//Calculate number of block groups
	numGroups = (blockCount / blocksPerGroup);
	int lastBlockSize = (blockCount % blocksPerGroup);

	if (lastBlockSize > 0)
		numGroups++;

	//printf("num groups %d, leftover blocks %d, blocks per group %d\n", numGroups, blockCount % blocksPerGroup, blocksPerGroup);

	groupDescriptors = malloc(numGroups * sizeof(struct groupDescriptorFields));
	if (groupDescriptors == 0) {
		fprintf(stderr, "Memory allocation error at 161\n");
	}

	int i;
	//read and store group descriptor values for each group
	for (i = 0; i < numGroups; i++){
		//Read and store number of free blocks
		preadLittleEndian(fd, buffer, 2, startGroupDescriptor + (32*i) + 12); //Offset for free blocks
		groupDescriptors[i].freeBlockCount = getIntFromBuffer(buffer, 2);

		//Calculate number of contained blocks
		if (i == numGroups - 1 && lastBlockSize > 0)
			groupDescriptors[i].containedBlockCount = lastBlockSize;
		else
			groupDescriptors[i].containedBlockCount = blocksPerGroup;

		//Read and store number of free inodes
		preadLittleEndian(fd, buffer, 2, startGroupDescriptor + (32*i) + 14); //Offset for free inodes
		groupDescriptors[i].freeInodeCount = getIntFromBuffer(buffer, 2);

		//Read and store number of directories
		preadLittleEndian(fd, buffer, 2, startGroupDescriptor + (32*i) + 16); //Offset for directories
		groupDescriptors[i].directoryCount = getIntFromBuffer(buffer, 2);

		//allocate space for hex values
		groupDescriptors[i].inodeBitmapBlock = malloc(4 * sizeof(char));
		groupDescriptors[i].blockBitmapBlock = malloc(4 * sizeof(char));
		groupDescriptors[i].inodeTableBlock = malloc(4 * sizeof(char));

		preadLittleEndian(fd, groupDescriptors[i].inodeBitmapBlock, 4, startGroupDescriptor + (32*i) + 4); //Offset for inode bitmap block
		groupDescriptors[i].inodeBitmapBlock[4] = '\0';

		preadLittleEndian(fd, groupDescriptors[i].blockBitmapBlock, 4, startGroupDescriptor + (32*i)); //Offset for inode bitmap block
		groupDescriptors[i].blockBitmapBlock[4] = '\0';

		preadLittleEndian(fd, groupDescriptors[i].inodeTableBlock, 4, startGroupDescriptor + (32*i) + 8); //Offset for inode table block
		groupDescriptors[i].inodeTableBlock[4] = '\0';

		//print stuff
		fprintf(writeFileStream, "%d,%d,%d,%d\n", 
			groupDescriptors[i].containedBlockCount, groupDescriptors[i].freeBlockCount, 
			groupDescriptors[i].freeInodeCount, groupDescriptors[i].directoryCount);
	}
}

//Prints the block number of a free block and the map that free information came from
//into the corresponding csv. 
void readFreeBitmapEntry(int fd) {
	FILE* writeFileStream = fopen("bitmap.csv", "w+");

	listOfAllocatedInodes = malloc(inodeCount * sizeof(unsigned long));
	if (listOfAllocatedInodes == 0) {
		fprintf(stderr, "Memory allocation error at 213\n");
	}
	allocatedInodeCount = 0;

	//For each group...
	for (int group = 0; group < numGroups; group++) {
		struct groupDescriptorFields fields = groupDescriptors[group];
		//For the data bitmap...
		//Extract data bitmap location
		unsigned long blockBitmapBlock = getIntFromBuffer(fields.blockBitmapBlock, 4);
		

		//CRITICAL ASSUMPTION: BLOCK BITMAP CONTAINS METADATA BLOCKS
		//CRITICAL ASSUMPTION: EMPTY BLOCK IS NOT INCLUDED IN BLOCK BITMAP


		//Obtain which index the first data block of this group corresponds to
		//unsigned long dataBlockStartOffset = inodeTableBlock 
		//	+ (bytesPerInode * inodesPerGroup + blockSize - 1) / blockSize;
			//The block number of the start of the inode table + the size of the inode table
			//in blocks
		unsigned long dataBlockStartOffset = 
			1 + group * groupDescriptors[0].containedBlockCount;
		unsigned long blockBitmapByteOffset = blockBitmapBlock * blockSize;

		for (int i = 0; i < fields.containedBlockCount; i++) {
			unsigned char buffer;
			pread(fd, &buffer, 1, blockBitmapByteOffset + i / 8);
			unsigned int bitmask = 0x1 << (i % 8);
			if (!(bitmask & buffer)) {
				//Found an empty data block, mark accordingly
				fprintf(writeFileStream, "%lx,%lu\n", blockBitmapBlock,
					dataBlockStartOffset + i);
			}
		}


		//For the inode bitmap...
		//Extract inode bitmap location
		unsigned long inodeBitmapBlock = getIntFromBuffer(fields.inodeBitmapBlock, 4);

		//Obtain some inode block offset start location //TODO
		unsigned long inodeStartOffset = 1 + group * inodesPerGroup;
		unsigned long inodeByteStartOffset = inodeBitmapBlock * blockSize;

		for (int i = 0; i < inodesPerGroup; i++) {
			unsigned char buffer;
			pread(fd, &buffer, 1, inodeByteStartOffset + i / 8);
			unsigned int bitmask = 0x1 << (i % 8);
			if (!(bitmask & buffer)) {
				//Found an empty data block, mark accordingly
				fprintf(writeFileStream, "%lx,%lu\n", inodeBitmapBlock,
					inodeStartOffset + i);
			}
			else {
				listOfAllocatedInodes[allocatedInodeCount] = inodeStartOffset + i;
				allocatedInodeCount++;
			}
		}
	}
	fflush(writeFileStream);
}

//Reads in inodes, populates pointers to denote found indoes with certain properties, 
//and creates the csv
void readInodes(int fd) {
	FILE* writeFileStream = fopen("inode.csv", "w+");

	listOfDirectoryInodes = malloc(allocatedInodeCount * sizeof(unsigned long));
	directoryInodeCount = 0;

	if (listOfDirectoryInodes == 0) {
		fprintf(stderr, "Memory allocation error at 281\n");
	}

	listOfIndirectInodes = malloc(allocatedInodeCount * sizeof(unsigned long));
	indirectInodeCount = 0;

	if (listOfIndirectInodes == 0) {
		fprintf(stderr, "Memory allocation error at 282\n");
	}

	//From the list of populated inodes created in readFreeBitmapEntry, iterate through
	//populated list. 

	for (int i = 0; i < allocatedInodeCount; i++) {
		unsigned long currentInodeNumber = listOfAllocatedInodes[i];
		//Locate the inode from the inode number
		unsigned long blockGroup = (currentInodeNumber - 1) / inodesPerGroup;
		unsigned long localInodeIndex = (currentInodeNumber - 1) % inodesPerGroup;

		unsigned long inodeByteOffset = 
			getIntFromBuffer(groupDescriptors[blockGroup].inodeTableBlock, 4)
				* blockSize //The byte offset of the inode table for this particular inode
							//(created by block number of that table * bytes per block)
			+ localInodeIndex * bytesPerInode; //The number of bytes into the table this
											   //inode resides

		//Write inode number to inode.csv
		fprintf(writeFileStream, "%lu,", currentInodeNumber);

		//Get the file type of the inode. We only care about regular files 'f', 
		//directories 'd', and symbolic links 's'. Everything else is marked with '?'.
		unsigned char buffer[64];
		preadLittleEndian(fd, buffer, 2, inodeByteOffset + 0); //Offset for mode info
		unsigned int modeInfo = getIntFromBuffer(buffer, 2);
		if ((modeInfo & 0xA000) == 0xA000) //Symbolic link 
			fprintf(writeFileStream, "s,");
		else if ((modeInfo & 0x8000) == 0x8000) //Regular file
			fprintf(writeFileStream, "f,");
		else if ((modeInfo & 0x4000) == 0x4000) { 
			//Directory. Also needs to add this into directory 
			//structure
			fprintf(writeFileStream, "d,");
			listOfDirectoryInodes[directoryInodeCount] = currentInodeNumber;
			directoryInodeCount++;
		}
		else fprintf(writeFileStream, "?,");

		//Print the full mode info
		fprintf(writeFileStream, "%o,", modeInfo);

		//Print owner info
		preadLittleEndian(fd, buffer, 2, inodeByteOffset + 2); //Offset for UID
		fprintf(writeFileStream, "%d,", getIntFromBuffer(buffer, 2));

		//Print group id
		preadLittleEndian(fd, buffer, 2, inodeByteOffset + 24); //Offset for GID
		fprintf(writeFileStream, "%d,", getIntFromBuffer(buffer, 2));

		//Print link count
		preadLittleEndian(fd, buffer, 2, inodeByteOffset + 26); //Offset for link count
		fprintf(writeFileStream, "%d,", getIntFromBuffer(buffer, 2));

		//Creation time (hexedecimal)
		preadLittleEndian(fd, buffer, 4, inodeByteOffset + 12); //Offset for creation time
		fprintf(writeFileStream, "%x,", getIntFromBuffer(buffer, 4)); //Written in hex

		//Modification time (hexedecimal)
		preadLittleEndian(fd, buffer, 4, inodeByteOffset + 16); //Offset for Modification time
		fprintf(writeFileStream, "%x,", getIntFromBuffer(buffer, 4)); //Written in hex

		//Access time (hexedecimal)
		preadLittleEndian(fd, buffer, 4, inodeByteOffset + 8); //Offset for Access time
		fprintf(writeFileStream, "%x,", getIntFromBuffer(buffer, 4)); //Written in hex

		//File size
		preadLittleEndian(fd, buffer, 4, inodeByteOffset + 4); //Offset for lower 32 bytes
		unsigned int lower32 = getIntFromBuffer(buffer, 4);
		unsigned int upper32 = 0;
		if (modeInfo & 0x8000) { //Regular file, might have higher 32
			preadLittleEndian(fd, buffer, 4, inodeByteOffset + 108); 
				//Offset for upper 32 bytes
			upper32 = getIntFromBuffer(buffer, 4);
		}
		if (upper32 == 0) {
			fprintf(writeFileStream, "%u,", lower32);
		}
		else {
			unsigned long long total = ((unsigned long long) upper32 << 32) 
				+ (unsigned long long) lower32;
			fprintf(writeFileStream, "%llu,", total);
		}

		//File system block count
		preadLittleEndian(fd, buffer, 4, inodeByteOffset + 28);
		unsigned int smallBlockChunks = getIntFromBuffer(buffer, 4);
		//Number of file system blocks is dependent on the block size
		unsigned int blockChunk = 
			(smallBlockChunks * 512 + blockSize - 1) / (blockSize); //Block size rounded up
		fprintf(writeFileStream, "%u", blockChunk); //Note no comma here due to how
													//we do block pointer separation

		//Block pointers
		unsigned int blockPointerArrayOffset = 40;
		int added = 0;
		for (int i = 0; i < 15; i++) {
			//Read the ith pointer. Each pointer is 4 bytes wide. 
			preadLittleEndian(fd, buffer, 4, 
				inodeByteOffset + blockPointerArrayOffset + i * 4);
			//First add a comma, then write the pointer
			int pointerValue = getIntFromBuffer(buffer, 4);
			fprintf(writeFileStream, ",%x", pointerValue);
			if (pointerValue != 0 && i >= 12 && !added) { 
				//Pointer 12 on points to indirect pointers and we haven't already added it
				listOfIndirectInodes[indirectInodeCount] = currentInodeNumber;
				indirectInodeCount++;
				added = 1;
			}
		}
		fprintf(writeFileStream, "\n");
		fflush(writeFileStream);
	}
}

void readDirectories(int fd){
	FILE* writeFileStream = fopen("directory.csv", "w+");
	//For every directory inode, loop
	//save field values here
	int i, j;
	unsigned long parentDirInode;
	int entryNo;
	int entryLen = 0; //start at offset 0 when looking at directory entries
	int nameLen;
	int entryInode;
	unsigned char * name = malloc(256);

	//int lastDirectoryEntryFound = 0;

	unsigned char* blockNumBuffer = malloc(64);
	//unsigned char* directoryBlock = malloc(64);
	unsigned char* generalBuffer = malloc(64);

	for (i = 0; i < directoryInodeCount; i++){
		//get parent inode number
		parentDirInode = listOfDirectoryInodes[i];

		//find which group inode is in
		int parentBlockGroup = (parentDirInode - 1) / inodesPerGroup;
		//find local inode index
		int localParentIndex = (parentDirInode - 1) % inodesPerGroup;

		//locate parent inode
		int parentInodeOffset = (getIntFromBuffer(groupDescriptors[parentBlockGroup].inodeTableBlock, 4)) 
			+ (bytesPerInode * localParentIndex);

		//keep track of entry number
		entryNo = -1;
		entryLen = 0;

		//loop through blocks pointed to by parent inode
		for (j = 0; j < 15; j++){
			/*
			//check that last entry hasn't been found yet
			if (lastDirectoryEntryFound){
				break;
			}
			*/
			//copy data block number from parent inode
			pread(fd, &blockNumBuffer, 4, parentInodeOffset + 40 + (4*j));
			//if pointer is zero, no more data blocks to point to
			if (getIntFromBuffer(blockNumBuffer, 4) == 0){
				break;
			}
			//find block offset with directory entries
			int directoryBlockOffset = getIntFromBuffer(blockNumBuffer, 4) * blockSize;

			//keep track of where in block the current spot is
			int currentEntryOffset = directoryBlockOffset;

			//for every entry, until last entry is found or reach end of block
			while ((currentEntryOffset - directoryBlockOffset) < blockSize){
				//increment entry number - this is why it starts at -1, because the first entry is 0
				entryNo++;

				//Read inode number of entry
				pread(fd, &generalBuffer, 4, currentEntryOffset);
				entryInode = getIntFromBuffer(generalBuffer, 4);

				//Read rec_len
				pread(fd, &generalBuffer, 2, currentEntryOffset + 4);
				entryLen = getIntFromBuffer(generalBuffer, 2);

				//If inode number is 0, stop recording info, increment current offset, move onto next entry
				if (entryInode == 0){
					currentEntryOffset += entryLen;
					continue;
				}

				//else, continue recording and print info
				//get name length
				pread(fd, &generalBuffer, 1, currentEntryOffset + 6);
				nameLen = getIntFromBuffer(generalBuffer, 1);

				//get name
				pread(fd, &name, nameLen, currentEntryOffset + 8);
				name[nameLen] = '\0';

				//print entry info
				fprintf(writeFileStream, "%ld,%d,%d,%d,%d,%s\n", 
					parentDirInode, entryNo, entryLen, nameLen, entryInode, name);

				//increment current offset
				currentEntryOffset += entryLen;
			} //end entry-reading loop
		} //end block-traversing loop
	} //end directory-traversing loop
}

int main (int argc, const char* argv[]) {
	if (argc != 2) {
		fprintf(stderr, "%s: Usage: %s [disk-image-file-name]\n", 
			argv[0], argv[0]);
		exit(1);
	}

	int diskImageFD = open(argv[1], O_RDONLY | O_LARGEFILE);
	if (diskImageFD == -1) {
		perror(argv[0]);
		exit(1);
	}

	unsigned char* buf = malloc(4);
	preadLittleEndian(diskImageFD, buf, 4, 1064);
	//printf("%02X:%02X:%02X:%02X\n", buf[0], buf[1], buf[2], buf[3]);

	//printf("%d\n", getIntFromBuffer(buf, 4));

	readSuperBlock(diskImageFD);
	readGroupDescriptor(diskImageFD);
	readFreeBitmapEntry(diskImageFD);
	readInodes(diskImageFD);
}