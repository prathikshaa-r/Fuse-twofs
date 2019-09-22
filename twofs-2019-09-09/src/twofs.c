/*
  Two FS: Only Two Files in whatever block device is provided
  file1 0666
  file2 0666
*/

#include "config.h"
#include "params.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuse.h>
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef HAVE_SYS_XATTR_H
#include <sys/xattr.h>
#endif

#include "log.h"

static const char *file1 = "file1";
static const char *file2 = "file2";
static ssize_t block_device_size = 0;

int starts_with(char *str, char *substr) {
  if (strncmp(str, substr, strlen(substr)) == 0)
    return 1;
  else
    return 0;
}

int ends_with(char *str, const char *substr) {
  log_msg("ends_with:\n");
  char *substr_ptr = strstr(str, substr);
  if (substr_ptr == NULL) {
    log_msg("    not a match\n");
    return 0;
  } else if (sizeof(substr_ptr) == sizeof(substr)) {
    log_msg("    string sizes match\n");
    return 1;
  } else {
    log_msg("    string sizes mismatch\n");
    return 0;
  }
}

//  All the paths I see are relative to the root of the mounted
//  filesystem.  In order to get to the underlying filesystem, I need to
//  have the mountpoint.  I'll save it away early on in main(), and then
//  whenever I need a path for something I'll call this to construct
//  it.
static void bb_fullpath(char fpath[PATH_MAX], const char *path) {
  strcpy(fpath, BB_DATA->rootdir);
  strncat(fpath, path, PATH_MAX); // ridiculously long paths will
                                  // break here

  log_msg("    bb_fullpath:  rootdir = \"%s\", path = \"%s\", fpath = \"%s\"\n",
          BB_DATA->rootdir, path, fpath);
}

///////////////////////////////////////////////////////////
//
// Prototypes for all these functions, and the C-style comments,
// come from /usr/include/fuse.h
//
/** Get file attributes.
 *
 * Similar to stat().  The 'st_dev' and 'st_blksize' fields are
 * ignored.  The 'st_ino' field is ignored except if the 'use_ino'
 * mount option is given.
 */
int bb_getattr(const char *path, struct stat *statbuf) {
  int retstat = 0;
  char fpath[PATH_MAX];
  bb_fullpath(fpath, path);

  if (strcmp(path, "/") == 0) { // mount directory
    log_msg("\nbb_getattr(path=\"%s\", statbuf=0x%08x)\n", path, statbuf);
    //    retstat = log_syscall("lstat", lstat(fpath, statbuf), 0);
    statbuf->st_mode = S_IFDIR | 0666; // read/write directory
    statbuf->st_nlink = 1;             // no. of hard links = 1
    statbuf->st_size = block_device_size;
    log_stat(statbuf);
    return 0;
  } else if (ends_with(fpath, file1)) { // file1
    statbuf->st_mode = S_IFREG | 0666;
    statbuf->st_nlink = 1;
    statbuf->st_size = block_device_size / 2;
  } else if (ends_with(fpath, file2)) { // file2
    statbuf->st_mode = S_IFREG | 0666;
    statbuf->st_nlink = 1;
    statbuf->st_size = block_device_size / 2;
  } else {
    return -ENOENT;
  }
  return retstat;
}
// ignoring some of chown, chmod, mkdir, rmdir, etc.

int bb_truncate(const char *path, off_t length) {
  if (ends_with((char *)path, file1)) { // file1
    return 0;
  } else if (ends_with((char *)path, file2)) { // file2
    return 0;
  }
  return -1;
}

/** File open operation
 *
 * No creation, or truncation flags (O_CREAT, O_EXCL, O_TRUNC)
 * will be passed to open().  Open should check if the operation
 * is permitted for the given flags.  Optionally open may also
 * return an arbitrary filehandle in the fuse_file_info structure,
 * which will be passed to all file operations.
 *
 * Changed in version 2.2
 */
int bb_open(const char *path, struct fuse_file_info *fi) {
  int retstat = 0;
  int fd;
  char fpath[PATH_MAX];

  log_msg("\nbb_open(path\"%s\", fi=0x%08x)\n", path, fi);
  bb_fullpath(fpath, path);

  if ((ends_with(fpath, file1)) || (ends_with(fpath, file2))) {
    // open block device and save the relevant fd
    log_msg("*** bb_open on file or file2 ***\n");

    // if the open call succeeds, my retstat is the file descriptor,
    // else it's -errno.  I'm making sure that in that case the saved
    // file descriptor is exactly -1.
    fd = log_syscall("open", open(BB_DATA->rootdir, fi->flags), 0);
    if (fd < 0)
      retstat = log_error("open");
    fi->fh = fd;
    log_fi(fi);
    return retstat; // return 0 for success
  }
  if ((strcmp(path, "/") == 0) || (strcmp(fpath, BB_DATA->rootdir))) {
    //
  }
  return -ENOENT;
}

/** Read data from an open file
 *
 * Read should return exactly the number of bytes requested except
 * on EOF or error, otherwise the rest of the data will be
 * substituted with zeroes.  An exception to this is when the
 * 'direct_io' mount option is specified, in which case the return
 * value of the read system call will reflect the return value of
 * this operation.
 *
 * Changed in version 2.2
 */
// I don't fully understand the documentation above -- it doesn't
// match the documentation for the read() system call which says it
// can return with anything up to the amount of data requested. nor
// with the fusexmp code which returns the amount of data also
// returned by read.
int bb_read(const char *path, char *buf, size_t size, off_t offset,
            struct fuse_file_info *fi) {
  int retstat = 0;

  log_msg(
      "\nbb_read(path=\"%s\", buf=0x%08x, size=%d, offset=%lld, fi=0x%08x)\n",
      path, buf, size, offset, fi);
  // check if file1- check size + offset is within bounds
  if (ends_with((char *)path, file1)) { // file1
    if ((offset + size) >= (unsigned long)block_device_size / 2) {
      size = (block_device_size / 2) - offset;
    }
  }
  // check if file2 - update offset = offset + block_disk_size/2
  if (ends_with((char *)path, file2)) { // file1
    /* if ((offset + size) >= (unsigned long)block_device_size / 2) { */
    /*   size = (block_device_size / 2) - offset; */
    /* } */
    offset += (block_device_size / 2) + 1;
  }

  log_fi(fi);
  return log_syscall("pread", pread(fi->fh, buf, size, offset), 0);
}

/** Write data to an open file
 *
 * Write should return exactly the number of bytes requested
 * except on error.  An exception to this is when the 'direct_io'
 * mount option is specified (see read operation).
 *
 * Changed in version 2.2
 */
// As  with read(), the documentation above is inconsistent with the
// documentation for the write() system call.
int bb_write(const char *path, const char *buf, size_t size, off_t offset,
             struct fuse_file_info *fi) {
  int retstat = 0;

  log_msg("\nbb_write(path=\"%s\", buf=0x%08x, size=%d, offset=%lld, "
          "fi=0x%08x)\n",
          path, buf, size, offset, fi);
  // check if file1- check size + offset is within bounds
  if (ends_with((char *)path, file1)) { // file1
    //    offset = 0;
    if ((offset + size) >= (unsigned long)block_device_size / 2) {
      size = (block_device_size / 2) - offset;
    }
  }
  // check if file2 - update offset = offset + block_disk_size/2
  if (ends_with((char *)path, file2)) { // file1
    offset += (block_device_size / 2) + 1;
  }

  log_fi(fi);
  return log_syscall("pwrite", pwrite(fi->fh, buf, size, offset), 0);
}

/** Possibly flush cached data
 *
 * BIG NOTE: This is not equivalent to fsync().  It's not a
 * request to sync dirty data.
 *
 * Flush is called on each close() of a file descriptor.  So if a
 * filesystem wants to return write errors in close() and the file
 * has cached dirty data, this is a good place to write back data
 * and return any errors.  Since many applications ignore close()
 * errors this is not always useful.
 *
 * NOTE: The flush() method may be called more than once for each
 * open().  This happens if more than one file descriptor refers
 * to an opened file due to dup(), dup2() or fork() calls.  It is
 * not possible to determine if a flush is final, so each flush
 * should be treated equally.  Multiple write-flush sequences are
 * relatively rare, so this shouldn't be a problem.
 *
 * Filesystems shouldn't assume that flush will always be called
 * after some writes, or that if will be called at all.
 *
 * Changed in version 2.2
 */
// this is a no-op in BBFS.  It just logs the call and returns success
int bb_flush(const char *path, struct fuse_file_info *fi) {
  log_msg("\nbb_flush(path=\"%s\", fi=0x%08x)\n", path, fi);
  // no need to get fpath on this one, since I work from fi->fh not the path
  log_fi(fi);

  return 0;
}

/** Release an open file
 *
 * Release is called when there are no more references to an open
 * file: all file descriptors are closed and all memory mappings
 * are unmapped.
 *
 * For every open() call there will be exactly one release() call
 * with the same flags and file descriptor.  It is possible to
 * have a file opened more than once, in which case only the last
 * release will mean, that no more reads/writes will happen on the
 * file.  The return value of release is ignored.
 *
 * Changed in version 2.2
 */
int bb_release(const char *path, struct fuse_file_info *fi) {
  log_msg("\nbb_release(path=\"%s\", fi=0x%08x)\n", path, fi);
  log_fi(fi);

  // We need to close the file.  Had we allocated any resources
  // (buffers etc) we'd need to free them here as well.
  return log_syscall("close", close(fi->fh), 0);
}

/** Synchronize file contents
 *
 * If the datasync parameter is non-zero, then only the user data
 * should be flushed, not the meta data.
 *
 * Changed in version 2.2
 */
int bb_fsync(const char *path, int datasync, struct fuse_file_info *fi) {
  log_msg("\nbb_fsync(path=\"%s\", datasync=%d, fi=0x%08x)\n", path, datasync,
          fi);
  log_fi(fi);

  // some unix-like systems (notably freebsd) don't have a datasync call
#ifdef HAVE_FDATASYNC
  if (datasync)
    return log_syscall("fdatasync", fdatasync(fi->fh), 0);
  else
#endif
    return log_syscall("fsync", fsync(fi->fh), 0);
}

/** Open directory
 *
 * This method should check if the open operation is permitted for
 * this directory
 *
 * Introduced in version 2.3
 */
int bb_opendir(const char *path, struct fuse_file_info *fi) {
  // DIR *dp; // not required as we just return value based on path
  int retstat = 0;
  char fpath[PATH_MAX];

  log_msg("\nbb_opendir(path=\"%s\", fi=0x%08x)\n", path, fi);
  bb_fullpath(fpath, path);

  if (strcmp(path, "/") == 0) {
    log_msg("    bb_opendir on '\' or rootdir returns 0 for success.\n");
    return retstat;
  }

  log_msg("    bb_opendir on a directory other than the mountdir");
  return -ENOENT;
}

/** Read directory
 *
 * This supersedes the old getdir() interface.  New applications
 * should use this.
 *
 * The filesystem may choose between two modes of operation:
 *
 * 1) The readdir implementation ignores the offset parameter, and
 * passes zero to the filler function's offset.  The filler
 * function will not return '1' (unless an error happens), so the
 * whole directory is read in a single readdir operation.  This
 * works just like the old getdir() method.
 *
 * 2) The readdir implementation keeps track of the offsets of the
 * directory entries.  It uses the offset parameter and always
 * passes non-zero offset to the filler function.  When the buffer
 * is full (or an error happens) the filler function will return
 * '1'.
 *
 * Introduced in version 2.3
 */

int bb_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
               off_t offset, struct fuse_file_info *fi) {
  (void)offset; // not using offset

  int retstat = 0;
  char fpath[PATH_MAX];

  log_msg("\nbb_readdir(path=\"%s\", fi=0x%08x)\n", path, fi);
  bb_fullpath(fpath, path);
  if (strcmp(path, "/") == 0) {
    log_msg("    bb_readdir on '\' or rootdir returns 0 for success.\n");
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    filler(buf, file1, NULL, 0);
    filler(buf, file2, NULL, 0);
    return retstat;
  }

  log_msg("    bb_readdir on a directory other than the mountdir");
  return -ENOENT;
}

/** Release directory
 *
 * Introduced in version 2.3
 */
int bb_releasedir(const char *path, struct fuse_file_info *fi) {
  int retstat = 0;

  log_msg("\nbb_releasedir(path=\"%s\", fi=0x%08x)\n", path, fi);
  log_fi(fi);

  closedir((DIR *)(uintptr_t)fi->fh);

  return retstat;
}

/**
 * Initialize filesystem
 *
 * The return value will passed in the private_data field of
 * fuse_context to all file operations and as a parameter to the
 * destroy() method.
 *
 * Introduced in version 2.3
 * Changed in version 2.6
 */
// Undocumented but extraordinarily useful fact:  the fuse_context is
// set up before this function is called, and
// fuse_get_context()->private_data returns the user_data passed to
// fuse_main().  Really seems like either it should be a third
// parameter coming in here, or else the fact should be documented
// (and this might as well return void, as it did in older versions of
// FUSE).
void *bb_init(struct fuse_conn_info *conn) {
  log_msg("\nbb_init()\n");
  log_conn(conn);
  log_fuse_context(fuse_get_context());

  return BB_DATA;
}

/**
 * Clean up filesystem
 *
 * Called on filesystem exit.
 *
 * Introduced in version 2.3
 */
void bb_destroy(void *userdata) {
  log_msg("\nbb_destroy(userdata=0x%08x)\n", userdata);
}

/**
 * Get attributes from an open file
 *
 * This method is called instead of the getattr() method if the
 * file information is available.
 *
 * Currently this is only called after the create() method if that
 * is implemented (see above).  Later it may be called for
 * invocations of fstat() too.
 *
 * Introduced in version 2.5
 */
int bb_fgetattr(const char *path, struct stat *statbuf,
                struct fuse_file_info *fi) {
  int retstat = 0;
  log_msg("\nbb_fgetattr(path=\"%s\", statbuf=0x%08x, fi=0x%08x)\n", path,
          statbuf, fi);
  log_fi(fi);

  // On FreeBSD, trying to do anything with the mountpoint ends up
  // opening it, and then using the FD for an fgetattr.  So in the
  // special case of a path of "/", I need to do a getattr on the
  // underlying root directory instead of doing the fgetattr().
  return bb_getattr(path, statbuf);

  retstat = fstat(fi->fh, statbuf);
  if (retstat < 0)
    retstat = log_error("bb_fgetattr fstat");

  log_stat(statbuf);

  return retstat;
}

struct fuse_operations bb_oper = {
    .getattr = bb_getattr,
    .truncate = bb_truncate,
    //.readlink = bb_readlink,
    // no .getdir -- that's deprecated
    .getdir = NULL,
    //.mknod = bb_mknod,
    //.mkdir = bb_mkdir,
    //.unlink = bb_unlink,
    //.rmdir = bb_rmdir,
    //.symlink = bb_symlink,
    //.rename = bb_rename,
    //.link = bb_link,
    //.chmod = bb_chmod,
    //.chown = bb_chown,
    //.utime = bb_utime,
    .open = bb_open,
    .read = bb_read,
    .write = bb_write,
/** Just a placeholder, don't set */ // huh???
                                     //.statfs = bb_statfs,
                                     //.flush = bb_flush,
                                     //.release = bb_release,
                                     //.fsync = bb_fsync,

#ifdef HAVE_SYS_XATTR_H
//.setxattr = bb_setxattr,
//.getxattr = bb_getxattr,
//.listxattr = bb_listxattr,
//.removexattr = bb_removexattr,
#endif

    .opendir = bb_opendir,
    .readdir = bb_readdir,
    //.releasedir = bb_releasedir,
    //.fsyncdir = bb_fsyncdir,
    .init = bb_init,
    .destroy = bb_destroy,
    .fgetattr = bb_fgetattr
    //.access = bb_access,
    //.ftruncate = bb_ftruncate,
};

void bb_usage() {
  fprintf(
      stderr,
      "usage:  twofs [FUSE and mount options] <block device> <mountpoint>\n");
  abort();
}

int main(int argc, char *argv[]) {
  int fuse_stat; // fuse main return status
  struct bb_state *bb_data;

  if ((getuid() == 0) || (geteuid() == 0)) {
    fprintf(stderr, "Running twofs as root is a bad idea. And not allowed.\n");
    return 1;
  }

  // See which version of fuse we're running
  fprintf(stderr, "Fuse library version %d.%d\n", FUSE_MAJOR_VERSION,
          FUSE_MINOR_VERSION);

  if ((argc < 3) || (argv[argc - 2][0] == '-') || (argv[argc - 1][0] == '-')) {
    bb_usage();
  }

  bb_data = malloc(sizeof(struct bb_state));
  if (bb_data == NULL) {
    perror("main calloc");
    abort();
  }

  // save the block device in private data of fuse context
  bb_data->rootdir = realpath(argv[argc - 2], NULL);
  argv[argc - 2] = argv[argc - 1];
  argv[argc - 1] = NULL;
  argc--;

  bb_data->logfile = log_open();

  // stat on rootdir to get size of block device
  struct stat *stbuf;
  stbuf = malloc(sizeof(struct stat));
  fprintf(stderr, "before stat call on rootdir: %s\n", bb_data->rootdir);
  stat(bb_data->rootdir, stbuf);
  fprintf(stderr, "after stat call on rootdir\n");
  block_device_size = stbuf->st_size;
  fprintf(stderr, "block device size: %lu\n", block_device_size);

  // turn over control to fuse
  fprintf(stderr, "about to call fuse_main\n");
  fuse_stat = fuse_main(argc, argv, &bb_oper, bb_data);
  fprintf(stderr, "fuse_main returned %d\n", fuse_stat);

  return fuse_stat;
}
