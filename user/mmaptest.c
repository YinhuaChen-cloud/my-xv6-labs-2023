#include "kernel/param.h"
#include "kernel/fcntl.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/riscv.h"
#include "kernel/fs.h"
#include "user/user.h"

void mmap_test();
void fork_test();
char buf[BSIZE];

#define MAP_FAILED ((char *) -1)

int
main(int argc, char *argv[])
{
  mmap_test();
  fork_test();
  printf("mmaptest: all tests succeeded\n");
  exit(0);
}

char *testname = "???";

void
err(char *why)
{
  printf("mmaptest: %s failed: %s, pid=%d\n", testname, why, getpid());
  exit(1);
}

//
// check the content of the two mapped pages.
//
void
_v1(char *p)
{
  // 循环遍历两个页的所有字节，1.5个页为 A，0.5 个页为 0
  int i;
  for (i = 0; i < PGSIZE*2; i++) {
    if (i < PGSIZE + (PGSIZE/2)) {
      if (p[i] != 'A') {
        printf("mismatch at %d, wanted 'A', got 0x%x\n", i, p[i]);
        err("v1 mismatch (1)");
      }
    } else {
      if (p[i] != 0) {
        printf("mismatch at %d, wanted zero, got 0x%x\n", i, p[i]);
        err("v1 mismatch (2)");
      }
    }
  }
}

//
// create a file to be mapped, containing
// 1.5 pages of 'A' and half a page of zeros.
//
void
makefile(const char *f)
{
  int i;
  // n = 一个内存页能映射的块数
  int n = PGSIZE/BSIZE;

  // 删除文件 f
  unlink(f);
  // 创建文件 f
  int fd = open(f, O_WRONLY | O_CREATE);
  if (fd == -1)
    err("open");
  // 设置内存 buf 为全 A
  memset(buf, 'A', BSIZE);
  // write 1.5 page，即文件 f 拥有 1.5 个内存页的 A
  for (i = 0; i < n + n/2; i++) {
    if (write(fd, buf, BSIZE) != BSIZE)
      err("write 0 makefile");
  }
  // 关闭文件
  if (close(fd) == -1)
    err("close");
}

void
mmap_test(void)
{
  int fd;
  int i;
  const char * const f = "mmap.dur";
  printf("mmap_test starting\n");
  testname = "mmap_test";

  //
  // create a file with known content, map it into memory, check that
  // the mapped memory has the same bytes as originally written to the
  // file.
  //
  // 创建文件 f，内含 1.5 个内存页的 A 和 0.5 个内存页的 0
  makefile(f);
  if ((fd = open(f, O_RDONLY)) == -1)
    err("open (1)");

  printf("test mmap f\n");
  //
  // this call to mmap() asks the kernel to map the content
  // of open file fd into the address space. the first
  // 0 argument indicates that the kernel should choose the
  // virtual address. the second argument indicates how many
  // bytes to map. the third argument indicates that the
  // mapped memory should be read-only. the fourth argument
  // indicates that, if the process modifies the mapped memory,
  // that the modifications should not be written back to
  // the file nor shared with other processes mapping the
  // same file (of course in this case updates are prohibited
  // due to PROT_READ). the fifth argument is the file descriptor
  // of the file to be mapped. the last argument is the starting
  // offset in the file.
  //
  // void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
  // addr: 建议的映射起始地址，通常设为 NULL 让内核自动选择
  // length: 要映射的区域长度
  // prot: 内存保护标志，可以是以下值的组合：
  //   PROT_READ: 可读
  //   PROT_WRITE: 可写
  //   PROT_EXEC: 可执行
  //   PROT_NONE: 不可访问
  // flags: 映射类型和选项，常用值：
  //   MAP_SHARED: 共享映射，修改会写回文件
  //   MAP_PRIVATE: 私有映射，修改不会影响原文件(会读取文件上的内容，但不会修改原文件，也不会同步其它进程的更新)
  //   MAP_ANONYMOUS: 匿名映射，不与文件关联
  //   MAP_FIXED: 强制使用指定的地址
  // fd: 文件描述符，匿名映射时设为 -1
  // offset: 文件偏移量，通常为 0
  // 返回值:
  //   成功时返回映射区域的起始地址，失败返回 MAP_FAILED ((void *)-1)
  // 映射的进程内存起始地址由内核选择，映射长度为两个页，内存只读，私有映射，映射文件 f，偏移量为0
  char *p = mmap(0, PGSIZE*2, PROT_READ, MAP_PRIVATE, fd, 0);
  if (p == MAP_FAILED)
    err("mmap (1)");
  // 验证映射内存是否为 1.5 页A 和 0.5 页 0
  _v1(p);
  // int munmap(void *addr, size_t length); 
  // 用于解除内存映射
  // addr: 映射区域的起始地址
  // length: 要解除映射的区域长度
  // 返回值
  //   成功返回 0，失败返回 -1
  // 解除刚才的映射
  if (munmap(p, PGSIZE*2) == -1)
    err("munmap (1)");

  printf("test mmap f: OK\n");

  printf("test mmap private\n");
  // should be able to map file opened read-only with private writable
  // mapping
  // 一样的映射，只是映射的内存的操作权限变成可读可写 (一个只读打开的文件应该能使用 mmap PRIVATE 创建可写的内存)
  p = mmap(0, PGSIZE*2, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
  if (p == MAP_FAILED)
    err("mmap (2)");
  if (close(fd) == -1)
    err("close (1)");
  // 验证映射内容是否正确
  _v1(p);
  // 对映射内存修改
  for (i = 0; i < PGSIZE*2; i++)
    p[i] = 'Z';
  if (munmap(p, PGSIZE*2) == -1)
    err("munmap (2)");

  printf("test mmap private: OK\n");

  printf("test mmap read-only\n");

  // 如果是只读打开这个文件，那么 mmap 不允许 PROT_WRITE 和 MAP_SHARED 同时存在
  // check that mmap doesn't allow read/write mapping of a
  // file opened read-only.
  if ((fd = open(f, O_RDONLY)) == -1)
    err("open (2)");
  p = mmap(0, PGSIZE*3, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (p != MAP_FAILED)
    err("mmap (3)");
  if (close(fd) == -1)
    err("close (2)");

  printf("test mmap read-only: OK\n");

  printf("test mmap read/write\n");

  // 检测：如果文件是可读可写打开的，那么就可以让 mmap 能读写它
  // 注意：这里 mmap 映射了三页
  // check that mmap does allow read/write mapping of a
  // file opened read/write.
  if ((fd = open(f, O_RDWR)) == -1)
    err("open (3)");
  p = mmap(0, PGSIZE*3, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (p == MAP_FAILED)
    err("mmap (4)");
  if (close(fd) == -1)
    err("close (3)");

  // check that the mapping still works after close(fd).
  _v1(p);

  // write the mapped memory.
  for (i = 0; i < PGSIZE*2; i++)
    p[i] = 'Z';

  // unmap just the first two of three pages of mapped memory.
  if (munmap(p, PGSIZE*2) == -1)
    err("munmap (3)");

  printf("test mmap read/write: OK\n");

  printf("test mmap dirty\n");

  // 检查上一次测试的写入是否生效
  // check that the writes to the mapped memory were
  // written to the file.
  if ((fd = open(f, O_RDWR)) == -1)
    err("open (4)");
  for (i = 0; i < PGSIZE + (PGSIZE/2); i++){
    char b;
    if (read(fd, &b, 1) != 1)
      err("read (1)");
    if (b != 'Z')
      err("file does not contain modifications");
  }
  if (close(fd) == -1)
    err("close (4)");

  printf("test mmap dirty: OK\n");

  printf("test not-mapped unmap\n");

  // unmap the rest of the mapped memory.
  // 检测是否能分多次 unmap 去解除一次 mmap 的映射
  if (munmap(p+PGSIZE*2, PGSIZE) == -1)
    err("munmap (4)");

  printf("test not-mapped unmap: OK\n");

  printf("test mmap two files\n");

  // 检测是否能同时映射两个文件
  //
  // mmap two files at the same time.
  //
  int fd1;
  if((fd1 = open("mmap1", O_RDWR|O_CREATE)) < 0)
    err("open (5)");
  if(write(fd1, "12345", 5) != 5)
    err("write (1)");
  char *p1 = mmap(0, PGSIZE, PROT_READ, MAP_PRIVATE, fd1, 0);
  if(p1 == MAP_FAILED)
    err("mmap (5)");
  if (close(fd1) == -1)
    err("close (5)");
  if (unlink("mmap1") == -1)
    err("unlink (1)");

  int fd2;
  if((fd2 = open("mmap2", O_RDWR|O_CREATE)) < 0)
    err("open (6)");
  if(write(fd2, "67890", 5) != 5)
    err("write (2)");
  char *p2 = mmap(0, PGSIZE, PROT_READ, MAP_PRIVATE, fd2, 0);
  if(p2 == MAP_FAILED)
    err("mmap (6)");
  if (close(fd2) == -1)
    err("close (6)");
  if (unlink("mmap2") == -1)
    err("unlink (2)");

  if(memcmp(p1, "12345", 5) != 0)
    err("mmap1 mismatch");
  if(memcmp(p2, "67890", 5) != 0)
    err("mmap2 mismatch");

  if (munmap(p1, PGSIZE) == -1)
    err("munmap (5)");
  if(memcmp(p2, "67890", 5) != 0)
    err("mmap2 mismatch (2)");
  if (munmap(p2, PGSIZE) == -1)
    err("munmap (6)");

  printf("test mmap two files: OK\n");

  printf("mmap_test: ALL OK\n");
}

//
// mmap a file, then fork.
// check that the child sees the mapped file.
//
void
fork_test(void)
{
  int fd;
  int pid;
  const char * const f = "mmap.dur";

  printf("fork_test starting\n");
  testname = "fork_test";

  // 创建文件 f，映射文件两次 p1 p2
  // mmap the file twice.
  makefile(f);
  if ((fd = open(f, O_RDONLY)) == -1)
    err("open (7)");
  if (unlink(f) == -1)
    err("unlink (3)");
  char *p1 = mmap(0, PGSIZE*2, PROT_READ, MAP_SHARED, fd, 0);
  if (p1 == MAP_FAILED)
    err("mmap (7)");
  char *p2 = mmap(0, PGSIZE*2, PROT_READ, MAP_SHARED, fd, 0);
  if (p2 == MAP_FAILED)
    err("mmap (8)");

  // read just 2nd page.
  if(*(p1+PGSIZE) != 'A')
    err("fork mismatch (1)");

  // 子进程取消掉 p1 的映射
  if((pid = fork()) < 0)
    err("fork");
  if (pid == 0) {
    _v1(p1);
    if (munmap(p1, PGSIZE) == -1) // just the first page
      err("munmap (7)");
    exit(0); // tell the parent that the mapping looks OK.
  }

  // 等待子进程结束
  int status = -1;
  wait(&status);

  if(status != 0){
    printf("fork_test failed\n");
    exit(1);
  }

  // 检查 p1 p2 指向的映射内存里的内容是否不变
  // check that the parent's mappings are still there.
  _v1(p1);
  _v1(p2);

  printf("fork_test OK\n");
}
