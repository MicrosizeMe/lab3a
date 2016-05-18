#define _LARGEFILE64_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

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

		preadLittleEndian(fd, groupDescriptors[i].inodeTableBlock, 4, startGroupDescriptor + (32*i) + 4); //Offset for inode bitmap block
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
		}
	}
	fflush(writeFileStream);
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
}