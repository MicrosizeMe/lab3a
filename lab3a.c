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
int getIntFromBuffer(unsigned char* buffer, size_t count) {
	unsigned int returnInt = 0;
	for (int i = 0; i < count; i++) {
		returnInt = (returnInt << 8) + buffer[i];
	}
	return returnInt;
}

/*
void readWriteAndStoreInteger(int fd, char* buffer, size_t byteCount, int offset, int* value,
	FILE* writeFileStream1) {
	preadLittleEndian(fd, buffer, byteCount, offset); //Offset for inode count
	*value = getIntFromBuffer(buffer, byteCount);
	fprintf(writeFileStream, "%d,", *value);
}
*/

void readSuperBlock(int fd) {
	FILE* writeFileStream = fopen("super.csv", "w+");

	//Read magic number
	unsigned char* buffer = malloc(64); 
	preadLittleEndian(fd, buffer, 2, 1080); //1080 is where the magic number resides
		//no error checking
	buffer[2] = '\0';

	fprintf(writeFileStream, "%02x%02x,", 
		buffer[0], buffer[1]);

	//Read and store total number of inodes
	preadLittleEndian(fd, buffer, 4, 1024); //Offset for inode count
	inodeCount = getIntFromBuffer(buffer, 4);
	fprintf(writeFileStream, "%d,", inodeCount);

	//Read and store total number of blocks
	preadLittleEndian(fd, buffer, 4, 1028); //Offset for block count
	blockCount = getIntFromBuffer(buffer, 4);
	fprintf(writeFileStream, "%d,", blockCount);

	//Read and store block size
	preadLittleEndian(fd, buffer, 4, 1048); //Offset for block count
	blockSize = 1024 << getIntFromBuffer(buffer, 4);
	fprintf(writeFileStream, "%d,", blockSize);

	//Read and store fragment size
	preadLittleEndian(fd, buffer, 4, 1052); //Offset for fragment size
	fragmentSize = getIntFromBuffer(buffer, 4);
	if (fragmentSize > 0) {
		fragmentSize = 1024 << fragmentSize;
	}
	else fragmentSize = 1024 >> -1 * fragmentSize;
	fprintf(writeFileStream, "%d,", fragmentSize);

	//Read and store blocks per group
	preadLittleEndian(fd, buffer, 4, 1056); //Offset for blocks per group
	blocksPerGroup = getIntFromBuffer(buffer, 4);
	fprintf(writeFileStream, "%d,", blocksPerGroup);

	//Read and store inodes per group
	preadLittleEndian(fd, buffer, 4, 1064); //Offset for inodes per group
	inodesPerGroup = getIntFromBuffer(buffer, 4);
	fprintf(writeFileStream, "%d,", inodesPerGroup);

	//Read and store fragments per group
	preadLittleEndian(fd, buffer, 4, 1060); //Offset for fragments per group
	fragmentsPerGroup = getIntFromBuffer(buffer, 4);
	fprintf(writeFileStream, "%d,", fragmentsPerGroup);

	//Read and store first data block, as well as newline
	preadLittleEndian(fd, buffer, 4, 1044); //Offset for fist data block
	firstDataBlock = getIntFromBuffer(buffer, 4);
	fprintf(writeFileStream, "%d\n", firstDataBlock);

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
	printf("%02X:%02X:%02X:%02X\n", buf[0], buf[1], buf[2], buf[3]);

	printf("%d\n", getIntFromBuffer(buf, 4));

	readSuperBlock(diskImageFD);
}