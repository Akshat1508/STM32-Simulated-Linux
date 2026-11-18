/*
 * ============================================================================
 * File: vfs.c
 * Description: Virtual File System (VFS) Routing Layer Implementation
 * Target: ARM Cortex-M (QEMU MPS2 AN385 / Cortex-M3 / STM32)
 *
 * Implements unified POSIX file descriptors:
 *   - open, close: In-memory RAM file creation, lifecycle, descriptor assignment.
 *   - read, write: Routed to UART0 console, RAM buffers, sockets, or pipes.
 *   - lseek: Offset positioning on RAM files.
 *   - stat, fstat: File metadata and attributes inspection.
 *   - pipe: Bidirectional IPC backed by FreeRTOS stream buffers.
 *   - dup2: File descriptor aliasing and redirection.
 * ============================================================================
 */

#include "vfs.h"
#include <string.h>

/* LwIP headers for socket dispatch */
#include "lwip/sockets.h"

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

#define UART0_ADDRESS   (0x40004000UL)
#define UART0_DATA      (*( (volatile unsigned int *)(UART0_ADDRESS + 0UL) ))

static vfs_file_t     g_vfs_table[VFS_MAX_FDS];
static vfs_ram_file_t g_ram_files[VFS_MAX_RAM_FILES];
static int            g_vfs_initialized = 0;

void vfs_init(void)
{
    if (g_vfs_initialized) {
        return;
    }

    memset(g_vfs_table, 0, sizeof(g_vfs_table));
    memset(g_ram_files, 0, sizeof(g_ram_files));

    /* Provision standard streams: stdin, stdout, stderr */
    g_vfs_table[VFS_FD_STDIN].type      = VFS_NODE_CONSOLE;
    g_vfs_table[VFS_FD_STDIN].flags     = O_RDONLY;
    g_vfs_table[VFS_FD_STDIN].ref_count = 1;

    g_vfs_table[VFS_FD_STDOUT].type      = VFS_NODE_CONSOLE;
    g_vfs_table[VFS_FD_STDOUT].flags     = O_WRONLY;
    g_vfs_table[VFS_FD_STDOUT].ref_count = 1;

    g_vfs_table[VFS_FD_STDERR].type      = VFS_NODE_CONSOLE;
    g_vfs_table[VFS_FD_STDERR].flags     = O_WRONLY;
    g_vfs_table[VFS_FD_STDERR].ref_count = 1;

    g_vfs_initialized = 1;
}

static vfs_ram_file_t *vfs_find_ram_file(const char *pathname)
{
    for (int i = 0; i < VFS_MAX_RAM_FILES; i++) {
        if (g_ram_files[i].in_use) {
            if (strncmp(g_ram_files[i].name, pathname, VFS_MAX_PATH_LEN) == 0) {
                return &g_ram_files[i];
            }
        }
    }
    return NULL;
}

int open(const char *pathname, int flags, ...)
{
    if (!g_vfs_initialized) {
        vfs_init();
    }

    if (pathname == NULL) {
        errno = EFAULT;
        return -1;
    }

    /* Find existing file in in-memory RAM files */
    vfs_ram_file_t *file = NULL;
    int free_slot = -1;

    for (int i = 0; i < VFS_MAX_RAM_FILES; i++) {
        if (g_ram_files[i].in_use) {
            if (strncmp(g_ram_files[i].name, pathname, VFS_MAX_PATH_LEN) == 0) {
                file = &g_ram_files[i];
                break;
            }
        } else if (free_slot == -1) {
            free_slot = i;
        }
    }

    if (file == NULL) {
        /* File not found */
        if (flags & O_CREAT) {
            if (free_slot == -1) {
                errno = ENOSPC;
                return -1;
            }
            file = &g_ram_files[free_slot];
            memset(file, 0, sizeof(vfs_ram_file_t));
            strncpy(file->name, pathname, VFS_MAX_PATH_LEN - 1);
            file->in_use = 1;
        } else {
            errno = ENOENT;
            return -1;
        }
    } else {
        /* Existing file */
        if ((flags & O_CREAT) && (flags & O_EXCL)) {
            errno = EEXIST;
            return -1;
        }
        if (flags & O_TRUNC) {
            file->size = 0;
        }
    }

    /* Allocate free descriptor from user descriptor pool */
    int fd = -1;
    for (int i = VFS_USER_BASE_FD; i <= VFS_USER_MAX_FD; i++) {
        if (g_vfs_table[i].type == VFS_NODE_FREE) {
            fd = i;
            break;
        }
    }

    if (fd == -1) {
        errno = EMFILE;
        return -1;
    }

    g_vfs_table[fd].type      = VFS_NODE_RAMFILE;
    g_vfs_table[fd].flags     = flags;
    g_vfs_table[fd].offset    = (flags & O_APPEND) ? file->size : 0;
    g_vfs_table[fd].priv      = file;
    g_vfs_table[fd].ref_count = 1;

    return fd;
}

int close(int fd)
{
    if (!g_vfs_initialized) {
        vfs_init();
    }

    if (fd < 0 || fd >= VFS_MAX_FDS) {
        errno = EBADF;
        return -1;
    }

    vfs_file_t *f = &g_vfs_table[fd];
    if (f->type == VFS_NODE_FREE) {
        errno = EBADF;
        return -1;
    }

    if (f->type == VFS_NODE_CONSOLE) {
        return 0; /* Standard streams remain persistent */
    }

    /* Check if other descriptors still reference this underlying node/private data */
    int other_refs = 0;
    for (int i = 0; i < VFS_MAX_FDS; i++) {
        if (i != fd && g_vfs_table[i].type == f->type && g_vfs_table[i].priv == f->priv) {
            other_refs++;
        }
    }

    if (other_refs == 0) {
        if (f->type == VFS_NODE_SOCKET) {
            int lwip_s = (int)(uintptr_t)f->priv;
            lwip_close(lwip_s);
        } else if (f->type == VFS_NODE_PIPE_READER || f->type == VFS_NODE_PIPE_WRITER) {
            vfs_pipe_cb_t *pipe = (vfs_pipe_cb_t *)f->priv;
            if (pipe != NULL) {
                if (f->type == VFS_NODE_PIPE_READER) {
                    pipe->readers_open--;
                } else {
                    pipe->writers_open--;
                }
                if (pipe->readers_open <= 0 && pipe->writers_open <= 0) {
                    if (pipe->buffer != NULL) {
                        vStreamBufferDelete(pipe->buffer);
                    }
                    vPortFree(pipe);
                }
            }
        }
    }

    memset(f, 0, sizeof(vfs_file_t));
    return 0;
}

ssize_t read(int fd, void *buf, size_t count)
{
    if (!g_vfs_initialized) {
        vfs_init();
    }

    if (fd < 0 || fd >= VFS_MAX_FDS) {
        errno = EBADF;
        return -1;
    }

    if (buf == NULL && count > 0) {
        errno = EFAULT;
        return -1;
    }

    if (count == 0) {
        return 0;
    }

    vfs_file_t *f = &g_vfs_table[fd];
    if (f->type == VFS_NODE_FREE) {
        errno = EBADF;
        return -1;
    }

    switch (f->type) {
        case VFS_NODE_CONSOLE:
            /* Console stdin read: non-interactive batch environment returns EOF (0) */
            return 0;

        case VFS_NODE_SOCKET:
            return (ssize_t)lwip_read((int)(uintptr_t)f->priv, buf, count);

        case VFS_NODE_RAMFILE: {
            if ((f->flags & 0x03) == O_WRONLY) {
                errno = EBADF;
                return -1;
            }
            vfs_ram_file_t *file = (vfs_ram_file_t *)f->priv;
            if (file == NULL || !file->in_use) {
                errno = EIO;
                return -1;
            }
            if ((size_t)f->offset >= file->size) {
                return 0; /* EOF */
            }
            size_t available = file->size - (size_t)f->offset;
            size_t to_read = (count < available) ? count : available;
            memcpy(buf, &file->data[f->offset], to_read);
            f->offset += (off_t)to_read;
            return (ssize_t)to_read;
        }

        case VFS_NODE_PIPE_READER: {
            vfs_pipe_cb_t *pipe = (vfs_pipe_cb_t *)f->priv;
            if (pipe == NULL || pipe->buffer == NULL) {
                errno = EBADF;
                return -1;
            }
            if (pipe->writers_open <= 0 && xStreamBufferIsEmpty(pipe->buffer)) {
                return 0; /* All writers closed and buffer drained: EOF */
            }
            TickType_t wait_ticks = (f->flags & O_NONBLOCK) ? 0 : pdMS_TO_TICKS(1000);
            size_t rx_bytes = xStreamBufferReceive(pipe->buffer, buf, count, wait_ticks);
            if (rx_bytes == 0) {
                if (pipe->writers_open <= 0) {
                    return 0; /* All writers closed and buffer drained: EOF */
                }
                errno = EAGAIN;
                return -1;
            }
            return (ssize_t)rx_bytes;
        }

        default:
            errno = EBADF;
            return -1;
    }
}

ssize_t write(int fd, const void *buf, size_t count)
{
    if (!g_vfs_initialized) {
        vfs_init();
    }

    if (fd < 0 || fd >= VFS_MAX_FDS) {
        errno = EBADF;
        return -1;
    }

    if (buf == NULL && count > 0) {
        errno = EFAULT;
        return -1;
    }

    if (count == 0) {
        return 0;
    }

    vfs_file_t *f = &g_vfs_table[fd];
    if (f->type == VFS_NODE_FREE) {
        errno = EBADF;
        return -1;
    }

    switch (f->type) {
        case VFS_NODE_CONSOLE: {
            const char *p = (const char *)buf;
            for (size_t i = 0; i < count; i++) {
                UART0_DATA = (unsigned int)p[i];
            }
            return (ssize_t)count;
        }

        case VFS_NODE_SOCKET:
            return (ssize_t)lwip_write((int)(uintptr_t)f->priv, buf, count);

        case VFS_NODE_RAMFILE: {
            if ((f->flags & 0x03) == O_RDONLY) {
                errno = EBADF;
                return -1;
            }
            vfs_ram_file_t *file = (vfs_ram_file_t *)f->priv;
            if (file == NULL || !file->in_use) {
                errno = EIO;
                return -1;
            }
            if (f->flags & O_APPEND) {
                f->offset = (off_t)file->size;
            }
            if ((size_t)f->offset >= VFS_RAM_FILE_SIZE) {
                errno = ENOSPC;
                return -1;
            }
            size_t available = VFS_RAM_FILE_SIZE - (size_t)f->offset;
            size_t to_write = (count < available) ? count : available;
            memcpy(&file->data[f->offset], buf, to_write);
            f->offset += (off_t)to_write;
            if ((size_t)f->offset > file->size) {
                file->size = (size_t)f->offset;
            }
            return (ssize_t)to_write;
        }

        case VFS_NODE_PIPE_WRITER: {
            vfs_pipe_cb_t *pipe = (vfs_pipe_cb_t *)f->priv;
            if (pipe == NULL || pipe->buffer == NULL) {
                errno = EBADF;
                return -1;
            }
            if (pipe->readers_open <= 0) {
                errno = EPIPE;
                return -1; /* Broken pipe */
            }
            TickType_t wait_ticks = (f->flags & O_NONBLOCK) ? 0 : pdMS_TO_TICKS(1000);
            size_t tx_bytes = xStreamBufferSend(pipe->buffer, buf, count, wait_ticks);
            if (tx_bytes == 0) {
                errno = EAGAIN;
                return -1;
            }
            return (ssize_t)tx_bytes;
        }

        default:
            errno = EBADF;
            return -1;
    }
}

off_t lseek(int fd, off_t offset, int whence)
{
    if (!g_vfs_initialized) {
        vfs_init();
    }

    if (fd < 0 || fd >= VFS_MAX_FDS) {
        errno = EBADF;
        return (off_t)-1;
    }

    vfs_file_t *f = &g_vfs_table[fd];
    if (f->type == VFS_NODE_FREE) {
        errno = EBADF;
        return (off_t)-1;
    }

    if (f->type != VFS_NODE_RAMFILE) {
        /* POSIX: ESPIPE indicates fd is associated with a pipe, socket, or FIFO */
        errno = ESPIPE;
        return (off_t)-1;
    }

    vfs_ram_file_t *file = (vfs_ram_file_t *)f->priv;
    if (file == NULL || !file->in_use) {
        errno = EIO;
        return (off_t)-1;
    }

    off_t new_offset;
    switch (whence) {
        case SEEK_SET:
            new_offset = offset;
            break;
        case SEEK_CUR:
            new_offset = f->offset + offset;
            break;
        case SEEK_END:
            new_offset = (off_t)file->size + offset;
            break;
        default:
            errno = EINVAL;
            return (off_t)-1;
    }

    if (new_offset < 0) {
        errno = EINVAL;
        return (off_t)-1;
    }

    f->offset = new_offset;
    return f->offset;
}

int fstat(int fd, struct stat *statbuf)
{
    if (!g_vfs_initialized) {
        vfs_init();
    }

    if (statbuf == NULL) {
        errno = EFAULT;
        return -1;
    }

    if (fd < 0 || fd >= VFS_MAX_FDS) {
        errno = EBADF;
        return -1;
    }

    vfs_file_t *f = &g_vfs_table[fd];
    if (f->type == VFS_NODE_FREE) {
        errno = EBADF;
        return -1;
    }

    memset(statbuf, 0, sizeof(struct stat));

    switch (f->type) {
        case VFS_NODE_CONSOLE:
            statbuf->st_mode = S_IFCHR | 0666;
            statbuf->st_nlink = 1;
            statbuf->st_blksize = 1;
            return 0;

        case VFS_NODE_SOCKET:
            statbuf->st_mode = S_IFSOCK | 0666;
            statbuf->st_nlink = 1;
            return 0;

        case VFS_NODE_RAMFILE: {
            vfs_ram_file_t *file = (vfs_ram_file_t *)f->priv;
            if (file == NULL || !file->in_use) {
                errno = EIO;
                return -1;
            }
            statbuf->st_mode = S_IFREG | 0644;
            statbuf->st_size = (off_t)file->size;
            statbuf->st_nlink = 1;
            statbuf->st_blksize = 256;
            statbuf->st_blocks = (statbuf->st_size + 255) / 256;
            return 0;
        }

        case VFS_NODE_PIPE_READER:
        case VFS_NODE_PIPE_WRITER:
            statbuf->st_mode = S_IFIFO | 0600;
            statbuf->st_nlink = 1;
            return 0;

        default:
            errno = EBADF;
            return -1;
    }
}

int stat(const char *pathname, struct stat *statbuf)
{
    if (!g_vfs_initialized) {
        vfs_init();
    }

    if (pathname == NULL || statbuf == NULL) {
        errno = EFAULT;
        return -1;
    }

    if (strcmp(pathname, "/dev/stdin") == 0 ||
        strcmp(pathname, "/dev/stdout") == 0 ||
        strcmp(pathname, "/dev/stderr") == 0 ||
        strcmp(pathname, "/dev/console") == 0) {
        memset(statbuf, 0, sizeof(struct stat));
        statbuf->st_mode = S_IFCHR | 0666;
        statbuf->st_nlink = 1;
        statbuf->st_blksize = 1;
        return 0;
    }

    vfs_ram_file_t *file = vfs_find_ram_file(pathname);
    if (file == NULL) {
        errno = ENOENT;
        return -1;
    }

    memset(statbuf, 0, sizeof(struct stat));
    statbuf->st_mode = S_IFREG | 0644;
    statbuf->st_size = (off_t)file->size;
    statbuf->st_nlink = 1;
    statbuf->st_blksize = 256;
    statbuf->st_blocks = (statbuf->st_size + 255) / 256;
    return 0;
}

int pipe(int pipefd[2])
{
    if (!g_vfs_initialized) {
        vfs_init();
    }

    if (pipefd == NULL) {
        errno = EFAULT;
        return -1;
    }

    int fd0 = -1, fd1 = -1;
    for (int i = VFS_USER_BASE_FD; i <= VFS_USER_MAX_FD; i++) {
        if (g_vfs_table[i].type == VFS_NODE_FREE) {
            if (fd0 == -1) {
                fd0 = i;
            } else if (fd1 == -1) {
                fd1 = i;
                break;
            }
        }
    }

    if (fd0 == -1 || fd1 == -1) {
        errno = EMFILE;
        return -1;
    }

    StreamBufferHandle_t buf = xStreamBufferCreate(128, 1);
    if (buf == NULL) {
        errno = ENOMEM;
        return -1;
    }

    vfs_pipe_cb_t *pipe_cb = (vfs_pipe_cb_t *)pvPortMalloc(sizeof(vfs_pipe_cb_t));
    if (pipe_cb == NULL) {
        vStreamBufferDelete(buf);
        errno = ENOMEM;
        return -1;
    }

    pipe_cb->buffer = buf;
    pipe_cb->readers_open = 1;
    pipe_cb->writers_open = 1;

    /* Reader descriptor (pipefd[0]) */
    g_vfs_table[fd0].type = VFS_NODE_PIPE_READER;
    g_vfs_table[fd0].flags = O_RDONLY;
    g_vfs_table[fd0].offset = 0;
    g_vfs_table[fd0].priv = pipe_cb;
    g_vfs_table[fd0].ref_count = 1;

    /* Writer descriptor (pipefd[1]) */
    g_vfs_table[fd1].type = VFS_NODE_PIPE_WRITER;
    g_vfs_table[fd1].flags = O_WRONLY;
    g_vfs_table[fd1].offset = 0;
    g_vfs_table[fd1].priv = pipe_cb;
    g_vfs_table[fd1].ref_count = 1;

    pipefd[0] = fd0;
    pipefd[1] = fd1;
    return 0;
}

int dup2(int oldfd, int newfd)
{
    if (!g_vfs_initialized) {
        vfs_init();
    }

    if (oldfd < 0 || oldfd >= VFS_MAX_FDS || newfd < 0 || newfd >= VFS_MAX_FDS) {
        errno = EBADF;
        return -1;
    }

    vfs_file_t *old_f = &g_vfs_table[oldfd];
    if (old_f->type == VFS_NODE_FREE) {
        errno = EBADF;
        return -1;
    }

    if (oldfd == newfd) {
        return newfd;
    }

    /* If newfd is already open, close it first */
    if (g_vfs_table[newfd].type != VFS_NODE_FREE) {
        close(newfd);
    }

    /* Duplicate file descriptor entry */
    g_vfs_table[newfd] = *old_f;
    g_vfs_table[newfd].ref_count = 1;

    return newfd;
}
