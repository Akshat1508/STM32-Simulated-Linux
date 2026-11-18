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

    f->ref_count--;
    if (f->ref_count > 0) {
        return 0; /* Still referenced elsewhere via dup2 */
    }

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
            /* Receive bytes from stream buffer with non-blocking 0 timeout */
            size_t rx_bytes = xStreamBufferReceive(pipe->buffer, buf, count, 0);
            if (rx_bytes == 0 && pipe->writers_open <= 0) {
                return 0; /* All writers closed and buffer drained: EOF */
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
            size_t tx_bytes = xStreamBufferSend(pipe->buffer, buf, count, 0);
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
