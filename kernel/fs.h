// On-disk file system format.
// Both the kernel and user programs use this header file.


#define ROOTINO  1   // root i-number
#define BSIZE 1024  // block size

// Disk layout:
// [ boot block | super block | log | inode blocks |
//                                          free bit map | data blocks]
//
// mkfs computes the super block and builds an initial file system. The
// super block describes the disk layout:
struct superblock {
  uint magic;        // Must be FSMAGIC
  uint size;         // Size of file system image (blocks)
  uint nblocks;      // Number of data blocks
  uint ninodes;      // Number of inodes.
  uint nlog;         // Number of log blocks
  uint logstart;     // Block number of first log block
  uint inodestart;   // Block number of first inode block
  uint bmapstart;    // Block number of first free map block
};

#define FSMAGIC 0x10203040

// 将一级地址的个数从12 减少为 11 
#define SYMLINKDEPTH 10
#define NDIRECT 11
#define NINDIRECT (BSIZE / sizeof(uint))
#define NDINDIRECT (BSIZE / sizeof(uint)*BSIZE / sizeof(uint))
#define MAXFILE (NDIRECT + NINDIRECT + NDINDIRECT)


// On-disk inode structure
struct dinode {
  short type;           // 常见文件类型（如目录、文件、设备等），其值为 0 表示该 dinode 此刻空闲
  short major;          // 仅在 inode 表示设备文件时有效，主设备号用于标识设备的类型或类，例如硬盘、终端等
  short minor;          // 同样仅在 inode 表示设备文件时有效，次设备号用于标识同一类型设备中的不同设备实例。例如，不同的硬盘分区可能有不同的次设备号。
  short nlink;          // 当 nlink 变为 0 时，表示没有任何链接指向该 inode，系统可以将其回收
  uint size;            // 文件的大小
  uint addrs[NDIRECT+2];   // 文件内容所在的 block 编号
};

// 1 块 block 是 1024 字节
// 1个inode（index node）是 64 字节
// xv6将第 32 ～ 44 块 block 划分给 inodes
// Inodes per block.
// 每个磁盘块中可以存储的 inode 数量
#define IPB           (BSIZE / sizeof(struct dinode))

// Block containing inode i
#define IBLOCK(i, sb)     ((i) / IPB + sb.inodestart)

// Bitmap bits per block
#define BPB           (BSIZE*8)

// Block of free map containing bit for block b
#define BBLOCK(b, sb) ((b)/BPB + sb.bmapstart)

// Directory is a file containing a sequence of dirent structures.
#define DIRSIZ 14

struct dirent {
  ushort inum;
  char name[DIRSIZ];
};

