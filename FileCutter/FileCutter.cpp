#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <stdlib.h>

#include <cstdint>
#include <string>
#include <memory>
#include <algorithm>

void printHelp() {
	printf("FileCutter.exe -from <from_bytes> -length <length_bytes> <srcfile> <dstfile>\n");
}

int main(int argc, char* argv[]) {

	const char *srcpath = NULL, *dstpath = NULL;
	size_t fromBytes = 0, lengthBytes = 0;

	for (int i = 1; i < argc; ++i) {
		std::string inarg(argv[i]);
		if (inarg == "-from") {
			fromBytes = _atoi64(argv[++i]);
		}
		else if (inarg == "-length") {
			lengthBytes = _atoi64(argv[++i]);
		}
		else {
			if (srcpath == NULL) {
				srcpath = argv[i];
			}
			else if (dstpath == NULL) {
				dstpath = argv[i];
			}
			else {
				printHelp();
				return 1;
			}
		}
	}

	if (srcpath == NULL || dstpath == NULL) {
		printHelp();
		return 1;
	}

	enum { BUFSIZE = 4 * 1024 * 1024 };
	FILE* fpsrc = fopen(srcpath, "rb");
	FILE* fpdst = fopen(dstpath, "wb");
	auto buffer = std::unique_ptr<uint8_t>(new uint8_t[BUFSIZE]);
	size_t copyBytes = 0;
	_fseeki64(fpsrc, fromBytes, SEEK_SET);

	while (1) {
		size_t readBytes = fread(buffer.get(), 1, std::min<size_t>(BUFSIZE, lengthBytes - copyBytes), fpsrc);
		fwrite(buffer.get(), readBytes, 1, fpdst);
		if (readBytes < BUFSIZE) { break; }
		copyBytes += readBytes;
	}

	fclose(fpsrc);
	fclose(fpdst);

	printf("Š®—¹\n");

	return 0;
}
