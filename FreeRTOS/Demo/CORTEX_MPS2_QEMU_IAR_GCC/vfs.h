/*
 * ============================================================================
 * File: vfs.h
 * Description: Virtual File System (VFS) Routing Layer for STM32 Simulated Linux
 * Target: ARM Cortex-M (QEMU MPS2 AN385 / Cortex-M3 / STM32)
 *
 * Provides a unified POSIX file descriptor table (0..31) routing standard I/O:
 *   - FD 0, 1, 2: Standard streams (stdin, stdout, stderr -> UART0)
 *   - FD 3..15:   LwIP BSD Sockets (lwip_read, lwip_write, lwip_close)
 *   - FD 16..31:  In-Memory RAM Files & POSIX Pipes (FreeRTOS Stream Buffers)
 * ============================================================================
 */

#ifndef VFS_H
#define VFS_H

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "FreeRTOS.h"
#include "stream_buffer.h"

/* Standard POSIX File Open & Creation Flags */
#ifndef O_RDONLY
#define O_RDONLY        0x0000
#endif

#ifndef O_WRONLY
#define O_WRONLY        0x0001
#endif

#ifndef O_RDWR
#define O_RDWR          0x0002
#endif

#ifndef O_CREAT
#define O_CREAT         0x0040
#endif

#ifndef O_EXCL
#define O_EXCL          0x0080
#endif

#ifndef O_TRUNC
#define O_TRUNC         0x0200
#endif

#ifndef O_APPEND
#define O_APPEND        0x0400
#endif

/* Standard POSIX Seek Directives */
#ifndef SEEK_SET
#define SEEK_SET        0
#endif

#ifndef SEEK_CUR
#define SEEK_CUR        1
#endif

#ifndef SEEK_END
#define SEEK_END        2
#endif

/* POSIX File Type / Mode Bits */
#ifndef S_IFREG
#define S_IFREG         0100000
#endif

#ifndef S_IFCHR
#define S_IFCHR         0020000
#endif

#ifndef S_IFIFO
#define S_IFIFO         0010000
#endif

#ifndef S_IFSOCK
#define S_IFSOCK        0140000
#endif

/* VFS Descriptor Table Constants */
#define VFS_MAX_FDS             32

#define VFS_FD_STDIN             0
#define VFS_FD_STDOUT            1
#define VFS_FD_STDERR            2

#define VFS_SOCKET_BASE_FD       3
#define VFS_SOCKET_MAX_FD       15

#define VFS_USER_BASE_FD        16
#define VFS_USER_MAX_FD         31

/* In-Memory RAM Storage Configuration */
#define VFS_MAX_RAM_FILES        8
#define VFS_MAX_PATH_LEN        32
#define VFS_RAM_FILE_SIZE      256

/* Node Classification */
typedef enum {
    VFS_NODE_FREE = 0,
    VFS_NODE_CONSOLE,       /* stdin, stdout, stderr (UART0) */
    VFS_NODE_SOCKET,        /* LwIP sequential socket */
    VFS_NODE_RAMFILE,       /* In-memory file storage */
    VFS_NODE_PIPE_READER,   /* FreeRTOS stream buffer read endpoint */
    VFS_NODE_PIPE_WRITER    /* FreeRTOS stream buffer write endpoint */
} vfs_node_type_t;

/* In-Memory File Node */
typedef struct {
    char        name[VFS_MAX_PATH_LEN];
    uint8_t     data[VFS_RAM_FILE_SIZE];
    size_t      size;
    int         in_use;
} vfs_ram_file_t;

/* Pipe Control Block */
typedef struct {
    StreamBufferHandle_t    buffer;
    int                     readers_open;
    int                     writers_open;
} vfs_pipe_cb_t;

/* VFS Descriptor Structure */
typedef struct {
    vfs_node_type_t type;
    int             flags;          /* O_RDONLY, O_WRONLY, O_RDWR, etc. */
    off_t           offset;         /* Current read/write seek pointer */
    void           *priv;           /* Node-specific storage pointer */
    int             ref_count;      /* Reference count for dup2 */
} vfs_file_t;

/* Undefine LwIP socket macros that conflict with standard POSIX VFS symbols */
#ifdef read
#undef read
#endif

#ifdef write
#undef write
#endif

#ifdef close
#undef close
#endif

/* ============================================================================
 * Public VFS Core & POSIX File System API (Group E)
 * ============================================================================
 */

/**
 * @brief Initializes the VFS descriptor table and provisions standard streams.
 */
void vfs_init(void);

/**
 * @brief Opens or creates a file in the virtual filesystem.
 *
 * @param[in] pathname Path of the file (e.g. "/tmp/log.txt").
 * @param[in] flags    Access flags (O_RDONLY, O_WRONLY, O_RDWR, O_CREAT, O_TRUNC, O_APPEND).
 *
 * @return Non-negative file descriptor on success, or -1 on error (with errno set).
 */
int open(const char *pathname, int flags, ...);

/**
 * @brief Closes a file, socket, console, or pipe descriptor.
 *
 * @param[in] fd File descriptor to close.
 *
 * @return 0 on success, or -1 on error (with errno set).
 */
int close(int fd);

/**
 * @brief Reads up to count bytes from an open descriptor.
 *
 * @param[in]  fd    File descriptor.
 * @param[out] buf   Buffer to receive data.
 * @param[in]  count Maximum bytes to read.
 *
 * @return Number of bytes read, 0 on EOF, or -1 on error.
 */
ssize_t read(int fd, void *buf, size_t count);

/**
 * @brief Writes up to count bytes to an open descriptor.
 *
 * @param[in] fd    File descriptor.
 * @param[in] buf   Data buffer to write.
 * @param[in] count Number of bytes to write.
 *
 * @return Number of bytes written, or -1 on error.
 */
ssize_t write(int fd, const void *buf, size_t count);

/**
 * @brief Repositions read/write file offset.
 *
 * @param[in] fd     File descriptor.
 * @param[in] offset Position offset in bytes.
 * @param[in] whence SEEK_SET, SEEK_CUR, or SEEK_END.
 *
 * @return Resulting offset from beginning of file, or (off_t)-1 on error.
 */
off_t lseek(int fd, off_t offset, int whence);

/**
 * @brief Retrieves file status and metadata by path.
 *
 * @param[in]  pathname File path.
 * @param[out] statbuf  Structure to populate.
 *
 * @return 0 on success, or -1 on error.
 */
int stat(const char *pathname, struct stat *statbuf);

/**
 * @brief Retrieves file status and metadata by descriptor.
 *
 * @param[in]  fd      File descriptor.
 * @param[out] statbuf Structure to populate.
 *
 * @return 0 on success, or -1 on error.
 */
int fstat(int fd, struct stat *statbuf);

/**
 * @brief Creates an unidirectional pipe using FreeRTOS stream buffers.
 *
 * @param[out] pipefd Array of two ints: pipefd[0] (read end), pipefd[1] (write end).
 *
 * @return 0 on success, or -1 on error.
 */
int pipe(int pipefd[2]);

/**
 * @brief Duplicates an open file descriptor onto newfd.
 *
 * @param[in] oldfd Existing descriptor.
 * @param[in] newfd Target descriptor.
 *
 * @return newfd on success, or -1 on error.
 */
int dup2(int oldfd, int newfd);

#endif /* VFS_H */
