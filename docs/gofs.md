# GoFS

GoFS is a small filesystem stored in the raw disk image. The kernel uses it to
load `main.gex` at boot, and goof programs can create, replace and read files
in it.

## Disk layout

The disk image is 1.44 MiB, or 2880 sectors of 512 bytes each.

| Sector | Contents |
| --- | --- |
| 0 | BIOS boot sector |
| 1 | Kernel sector-count header |
| 2–128 | Kernel, up to 127 sectors |
| 129 | GoFS superblock |
| 130 | Root directory |
| 131 onward | File data |

The filesystem begins at sector 129. Sector numbers in GoFS directory entries
are relative to that start sector.

## On-disk format

All integer fields are little-endian. The superblock occupies one sector:

| Offset | Size | Value |
| --- | --- | --- |
| 0 | 4 bytes | `GOFS` magic |
| 4 | 2 bytes | Format version, currently `1` |
| 6 | 2 bytes | Number of root entries |
| 8 | 4 bytes | Root directory sector, relative to GoFS |
| 12 | 4 bytes | First data sector, relative to GoFS |
| 16 | 4 bytes | Filesystem size in sectors |

The root directory occupies one sector and has room for 16 entries. Each
32-byte entry contains:

| Offset | Size | Value |
| --- | --- | --- |
| 0 | 24 bytes | Null-terminated file name |
| 24 | 4 bytes | First data sector, relative to GoFS |
| 28 | 4 bytes | File size in bytes |

Files use contiguous sectors. The final sector may contain padding after the
file's declared size.

## Building and loading

`make` compiles the selected `PROGRAM` into `build/main.gex`, builds the kernel,
and creates `build/goofos.img`. `tools/mkgofs.py` writes the GoFS superblock,
root directory, and `main.gex` data into that image.

At boot, the kernel reads sectors through the primary master ATA disk using
LBA28 PIO. `gofs::read_file` checks the superblock and directory, then reads
the requested file into a caller-provided buffer. The kernel loads `main.gex`
this way and passes its bytes to the GEX runtime.

## Writing files

`gofs::write_file` creates a file or replaces its contents. The data is
written before the directory and superblock, so a failed write leaves the old
directory alone. The file's new data goes:

1. where the file already is, if the new data fits in its old sectors,
2. where the file already is, if it's the last file on disk (it grows in place),
3. otherwise after the last file's data.

Space a file moves away from isn't reused yet, so a disk that sees lots of
rewrites slowly fills up. Rebuilding the image starts fresh.

In goof, the shell's `write`, `read` and `files` commands use these built-in
funcs:

| func | does |
| --- | --- |
| `file_write(name, text)` | create or replace a file, gives `1` or `0` |
| `file_append(name, text)` | add to the end, creating the file if needed, gives `1` or `0` |
| `file_print(name)` | print the file, gives `0` if it doesn't exist |
| `file_list()` | print every file and its size, gives the number of files |

They refuse to write `main.gex`, so a program can't break the next boot.

Files written while GoFS is running are saved in the disk image itself, so
they're still there after `make run` restarts QEMU, until the image is rebuilt.

## Current limits

- Root directory only, no subdirectories.
- At most 16 files.
- Freed space isn't reused.
- Names are ASCII and limited to 23 bytes plus a null terminator.
- The kernel's `main.gex` buffer is 32 KiB, and goof can read or append to
  files up to 32 KiB.
