#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>

#include "vmu_driver.h"

// The Filesystem is only 128KB so just keep the entire thing in memory
static struct vmu_fs vmu_fs;


static int vmu_getattr(const char *path, struct stat *stbuf)
{
	memset(stbuf, 0, sizeof(const struct stat));

	/* Since VMU has a flat filesystem there is only one directory
	 * which is the root directory
	 */
	if (strcmp(path, "/") == 0) {
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
		return 0;
	}

	// Remove leading slash when checking filepaths
	if (strlen(path) > 0 && strstr(path, "/") == path)
		path++;

	// File doesn't exist as filename is too large
	if (strnlen(path, MAX_FILENAME_SIZE + 1) > MAX_FILENAME_SIZE)
		return -ENAMETOOLONG;

	int dir_entry = vmufs_get_dir_entry(&vmu_fs, path);

	// File not found
	if (dir_entry == -1)
		return -ENOENT;

	stbuf->st_mode = S_IFREG | 0777;
	stbuf->st_nlink = 1;
	stbuf->st_size = vmu_fs.vmu_file[dir_entry].size_in_blocks *
				BLOCK_SIZE_BYTES;

	stbuf->st_atime = get_creation_time(&vmu_fs.vmu_file[dir_entry]);
	stbuf->st_mtime = stbuf->st_atime;
	stbuf->st_ctime = stbuf->st_atime;

	return 0;
}


static int vmu_open(const char *path, struct fuse_file_info *file_info)
{
	if (strlen(path) > 0 && strstr(path, "/") == path)
	path++;

	int dir_entry = vmufs_get_dir_entry(&vmu_fs, path);

	if (dir_entry < 0)
		return -ENOENT;

	return 0;
}


static int vmu_read(const char *path, char *buf, size_t size, off_t offset,
	struct fuse_file_info *fi)
{
	// Remove leading slash when checking filepaths
	if (strlen(path) > 0 && strstr(path, "/") == path)
		path++;

	return vmufs_read_file(&vmu_fs, path, (uint8_t *)buf, size, offset);
}


static int vmu_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
	off_t offset, struct fuse_file_info *fi)
{
	// Flat filesystem
	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);

	// Locate the FAT directory entry for the file
	for (int i = TOTAL_DIRECTORY_ENTRIES - 1; i >= 0; i--) {
		if (!vmu_fs.vmu_file[i].is_free)
			filler(buf, vmu_fs.vmu_file[i].filename, NULL, 0);
	}

	return 0;
}


static int vmu_rename(const char *from, const char *to)
{
	return vmufs_rename_file(&vmu_fs, from, to);
}


static int vmu_write(const char *path, const char *buf, size_t size,
	off_t offset, struct fuse_file_info *fuse_file_info)
{
	// Remove leading slash when checking filepaths
	if (strlen(path) > 0 && strstr(path, "/") == path)
		path++;

	return vmufs_write_file(&vmu_fs, path, (uint8_t *)buf, size, offset);
}


static int vmu_unlink(const char *path)
{
	if (strlen(path) > 0 && strstr(path, "/") == path)
		path++;

	return vmufs_remove_file(&vmu_fs, path);
}


static int vmu_access(const char *path, int res)
{
	if (!strcmp("/", path))
		return 0;

	if (strlen(path) > 0 && strstr(path, "/") == path)
		path++;

	int dir_entry = vmufs_get_dir_entry(&vmu_fs, path);

	if (dir_entry < 0)
		return -ENOENT;

	return 0;
}


static int vmu_truncate(const char *path, off_t size)
{
	if (!strcmp("/", path))
		return 0;

	if (strlen(path) > 0 && strstr(path, "/") == path)
		path++;

	return vmufs_truncate_file(&vmu_fs, path, size);
}


static int vmu_utimens(const char *path, const struct timespec ts[2])
{
	// VMU FS doesn't store Last Accessed or Last Modified time
	return 0;
}


// We just pretend that the file is owned by the user who mounted the FS
static int vmu_chown(const char *path, uid_t uid, gid_t gid)
{
	return 0;
}


static int vmu_mknod(const char *path, mode_t mode, dev_t rdev)
{
	if (!strcmp("/", path))
		return 0;

	if (strlen(path) > 0 && strstr(path, "/") == path)
		path++;

	return vmu_fs_create_file(&vmu_fs, path);
}


static const struct fuse_operations fuse_operations = {
	.getattr = vmu_getattr,
	.open = vmu_open,
	.read = vmu_read,
	.readdir = vmu_readdir,
	.rename = vmu_rename,
	.unlink = vmu_unlink,
	.write = vmu_write,
	.access = vmu_access,
	.truncate = vmu_truncate,
	.utimens = vmu_utimens,
	.chown = vmu_chown,
	.mknod = vmu_mknod
};


int main(int argc, char *argv[])
{
	umask(0);
	uint8_t buf[BLOCK_SIZE_BYTES * TOTAL_BLOCKS + 1];

	if (argc < 3) {
		fprintf(stderr, "Usage: %s vmu_fs mount_point\n", argv[0]);
		return -1;
	}

	// Attempt to open the vmu filesystem provided
	const char *vmu_fs_filepath = argv[1];
	FILE *vmu_file = fopen(vmu_fs_filepath, "rb");

	if (vmu_file == NULL) {
		perror("Error");
		fprintf(stderr, "Unable to open file \"%s\"\n",
			vmu_fs_filepath);
		return -1;
	}

	// Attempt to read in the image into the buffer
	size_t length = fread(buf, sizeof(uint8_t),
		BLOCK_SIZE_BYTES * TOTAL_BLOCKS + 1, vmu_file);

	fclose(vmu_file);

	if (ferror(vmu_file) != 0) {
		perror("Error");
		fprintf(stderr, "Error reading file \"%s\"\n", vmu_fs_filepath);
		return -1;
	}

	// Attempt to parse the image into a vmu filesystem structure
	int fs_parse_result = vmufs_read_fs(buf, length, &vmu_fs);

	if (fs_parse_result != 0) {
		fprintf(stderr, "Unable to read VMU filesystem\n");
		return -1;
	}

	/* Need to swap mount point argv into the one before it was placed
	 * before passing control to fuse, otherwise fuse will think the
	 * vmu file is the mount point
	 */
	for (int i = 1; i < argc - 1; i++)
		argv[i] = argv[i + 1];

	argc--;

	int result = fuse_main(argc, argv, &fuse_operations, NULL);

	if (result == 0)
		return vmufs_write_changes_to_disk(&vmu_fs, vmu_fs_filepath);
	else
		return result;
}
