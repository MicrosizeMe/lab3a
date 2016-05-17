#define _LARGEFILE64_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>


//Since the file format is in a little endian format, it is useful to turn those bytes into 
//a big endian format (expected by the csv) procedurally. This function does this.
ssize_t preadLittleEndian(int fd, char* buffer, size_t count, off_t offset) {
	int readCount = pread(fd, buffer, count, offset);
	for (int i = 0; i < readCount / 2; i++) {
		char temp = buffer[i];
		buffer[i] = buffer[readCount - i - 1];
		buffer[readCount - i - 1] = temp;
	}
	return readCount;
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

	char* buf = malloc(4);
	preadLittleEndian(diskImageFD, buf, 4, 1024);
	printf("%02X:%02X:%02X:%02X\n", buf[0], buf[1], buf[2], buf[3]);
}