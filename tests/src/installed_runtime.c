/* Compile and execute inside an ordinary installed Darling runtime with CLT.
 * Uses shipping files only: no deployment manifest or injected lower fixtures. */
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/attr.h>
#include <sys/stat.h>
#include <unistd.h>

struct name_entry {
	char name[NAME_MAX + 1];
	unsigned int seen;
};
static struct name_entry names[4096];

static int compare_names(const void* left, const void* right)
{
	return strcmp(((const struct name_entry*)left)->name,
		((const struct name_entry*)right)->name);
}

static void check_directory(const char* path)
{
	DIR* directory = opendir(path);
	assert(directory != NULL);
	size_t count = 0;
	struct dirent* entry;
	errno = 0;
	while ((entry = readdir(directory)) != NULL) {
		if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
			continue;
		assert(count < sizeof(names) / sizeof(names[0]));
		assert(strlen(entry->d_name) <= NAME_MAX);
		strcpy(names[count].name, entry->d_name);
		names[count++].seen = 0;
	}
	assert(errno == 0);
	assert(closedir(directory) == 0);
	assert(count != 0);
	qsort(names, count, sizeof(names[0]), compare_names);
	for (size_t index = 1; index < count; ++index)
		assert(strcmp(names[index - 1].name, names[index].name) != 0);

	int fd = open(path, O_RDONLY | O_DIRECTORY);
	assert(fd >= 0);
	struct attrlist attributes = {
		.bitmapcount = ATTR_BIT_MAP_COUNT,
		.commonattr = ATTR_CMN_NAME,
	};
	for (unsigned int pass = 0; pass != 2; ++pass) {
		size_t total = 0;
		for (;;) {
			/* Small enough to force partial batches and seek-cookie rollback. */
			union { uint64_t alignment; unsigned char bytes[384]; } buffer;
			int received = getattrlistbulk(fd, &attributes, buffer.bytes,
				sizeof(buffer.bytes), 0);
			assert(received >= 0);
			if (received == 0)
				break;
			unsigned char* cursor = buffer.bytes;
			for (int index = 0; index < received; ++index) {
				uint32_t length;
				attrreference_t reference;
				assert(cursor + sizeof(length) + sizeof(reference) <=
					buffer.bytes + sizeof(buffer.bytes));
				memcpy(&length, cursor, sizeof(length));
				memcpy(&reference, cursor + sizeof(length), sizeof(reference));
				assert(length >= sizeof(length) + sizeof(reference));
				assert(cursor + length <= buffer.bytes + sizeof(buffer.bytes));
				assert(reference.attr_dataoffset >= (int)sizeof(reference));
				assert(reference.attr_length > 0);
				assert((size_t)reference.attr_dataoffset + reference.attr_length <=
					length - sizeof(length));
				const char* name = (const char*)cursor + sizeof(length) + reference.attr_dataoffset;
				assert(name[reference.attr_length - 1] == '\0');
				struct name_entry key;
				assert(strlen(name) <= NAME_MAX);
				strcpy(key.name, name);
				struct name_entry* found = bsearch(&key, names, count,
					sizeof(names[0]), compare_names);
				assert(found != NULL);
				assert(found->seen == pass);
				++found->seen;
				++total;
				cursor += length;
			}
		}
		assert(total == count);
		assert(lseek(fd, 0, SEEK_SET) == 0);
	}
	assert(close(fd) == 0);
}

static void check_macho(const char* path)
{
	int fd = open(path, O_RDONLY);
	assert(fd >= 0);
	uint32_t magic;
	assert(read(fd, &magic, sizeof(magic)) == sizeof(magic));
	assert(magic == 0xfeedface || magic == 0xfeedfacf ||
		magic == 0xcafebabe || magic == 0xbebafeca);
	assert(close(fd) == 0);
}

static void check_service(const char* path, mode_t expected_type)
{
	struct stat status;
	assert(stat(path, &status) == 0);
	assert((status.st_mode & S_IFMT) == expected_type);
	assert((status.st_mode & 0022) == 0);
}

int main(void)
{
	errno = 0;
	assert(mkdir("/System/Library/LaunchDaemons", 0755) == -1);
	assert(errno == EEXIST);
	check_macho("/usr/lib/libobjc-trampolines.dylib");
	check_macho("/usr/bin/security");
	check_macho("/usr/bin/sudo");
	check_macho("/System/Library/Frameworks/Metal.framework/Versions/A/Metal");
	check_service("/System/Library/LaunchDaemons/org.darlinghq.shellspawn.plist", S_IFREG);
	check_service("/System/Library/LaunchDaemons/com.apple.FileCoordination.plist", S_IFREG);
	check_service("/System/Library/LaunchDaemons", S_IFDIR);
	check_service("/System/Library/LaunchAgents", S_IFDIR);
	check_service("/Library/LaunchDaemons", S_IFDIR);
	check_service("/Library/LaunchAgents", S_IFDIR);
	struct stat data;
	assert(stat("/usr/share/icu/icudt66l.dat", &data) == 0);
	assert(data.st_size > 1024 * 1024);
	check_directory("/usr");
	check_macho("/usr/lib/swift/libswiftCore.dylib");
	check_macho("/System/Library/Frameworks/Combine.framework/Combine");
	check_macho("/System/Library/PrivateFrameworks/ArgumentParserInternal.framework/ArgumentParserInternal");
	check_macho("/System/Library/PrivateFrameworks/OSAnalytics.framework/OSAnalytics");
	check_directory("/usr/bin");
	check_directory("/usr/lib");
	check_directory("/System/Library/LaunchDaemons");
	puts("DARLING_INSTALLED_RUNTIME_OK");
	return 0;
}
