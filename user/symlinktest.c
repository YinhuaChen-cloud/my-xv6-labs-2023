#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/riscv.h"
#include "kernel/fcntl.h"
#include "kernel/spinlock.h"
#include "kernel/sleeplock.h"
#include "kernel/fs.h"
#include "kernel/file.h"
#include "user/user.h"

#define fail(msg) do {printf("FAILURE: " msg "\n"); failed = 1; goto done;} while (0);
static int failed = 0;

static void testsymlink(void);
static void concur(void);
static void cleanup(void);

int
main(int argc, char *argv[])
{
  // 删除之前调用 symlinktest 可能遗留的文件
  cleanup();
  // 测试串行场景下的符号链接
  testsymlink();
  // 测试并行场景下的符号链接
  concur();
  exit(failed);
}

static void
cleanup(void)
{
  unlink("/testsymlink/a");
  unlink("/testsymlink/b");
  unlink("/testsymlink/c");
  unlink("/testsymlink/1");
  unlink("/testsymlink/2");
  unlink("/testsymlink/3");
  unlink("/testsymlink/4");
  unlink("/testsymlink/z");
  unlink("/testsymlink/y");
  unlink("/testsymlink");
}

// stat a symbolic link using O_NOFOLLOW
static int
stat_slink(char *pn, struct stat *st)
{
  int fd = open(pn, O_RDONLY | O_NOFOLLOW);
  if(fd < 0)
    return -1;
  if(fstat(fd, st) != 0)
    return -1;
  return 0;
}

static void
testsymlink(void)
{
  int r, fd1 = -1, fd2 = -1;
  char buf[4] = {'a', 'b', 'c', 'd'};
  char c = 0, c2 = 0;
  struct stat st;
    
  printf("Start: test symlinks\n");

  mkdir("/testsymlink");

  // 创建 a
  fd1 = open("/testsymlink/a", O_CREATE | O_RDWR);
  if(fd1 < 0) fail("failed to open a");

  // 给 a 创建软链接 b
  r = symlink("/testsymlink/a", "/testsymlink/b");
  if(r < 0)
    fail("symlink b -> a failed");

  // 往 a 写入数据 abcd
  if(write(fd1, buf, sizeof(buf)) != 4)
    fail("failed to write to a");

  // 测试 b 的类型是否为软链接
  if (stat_slink("/testsymlink/b", &st) != 0)
    fail("failed to stat b");
  if(st.type != T_SYMLINK)
    fail("b isn't a symlink");

  // 打开文件 b，读取数据，看是否和之前写入 a 的数据一致
  fd2 = open("/testsymlink/b", O_RDWR);
  if(fd2 < 0)
    fail("failed to open b");
  read(fd2, &c, 1);
  if (c != 'a')
    fail("failed to read bytes from b");

  // 删除文件 a，此时预期打开文件 b 会报错
  unlink("/testsymlink/a");
  if(open("/testsymlink/b", O_RDWR) >= 0)
    fail("Should not be able to open b after deleting a");

  // 给文件 b 再次创建软链接 a
  r = symlink("/testsymlink/b", "/testsymlink/a");
  if(r < 0)
    fail("symlink a -> b failed");

  // 预期打开循环软链接会报错
  r = open("/testsymlink/b", O_RDWR);
  if(r >= 0)
    fail("Should not be able to open b (cycle b->a->b->..)\n");
  
  // 给不存在的文件创建软连接，预期会成功
  r = symlink("/testsymlink/nonexistent", "/testsymlink/c");
  if(r != 0)
    fail("Symlinking to nonexistent file should succeed\n");

  // 1->2->3->4
  r = symlink("/testsymlink/2", "/testsymlink/1");
  if(r) fail("Failed to link 1->2");
  r = symlink("/testsymlink/3", "/testsymlink/2");
  if(r) fail("Failed to link 2->3");
  r = symlink("/testsymlink/4", "/testsymlink/3");
  if(r) fail("Failed to link 3->4");

  // 关闭文件 a, b
  close(fd1);
  close(fd2);

  // 此时链接 1->2->3->4
  // 创建文件 4 预期成功，打开文件 1 预期成功
  fd1 = open("/testsymlink/4", O_CREATE | O_RDWR);
  if(fd1<0) fail("Failed to create 4\n");
  fd2 = open("/testsymlink/1", O_RDWR);
  if(fd2<0) fail("Failed to open 1\n");

  // 对文件 1 写入 # 字符，预期成功
  c = '#';
  r = write(fd2, &c, 1);
  if(r!=1) fail("Failed to write to 1\n");
  // 读取文件 4 的 1 个字符，预期成功，应该是 #
  r = read(fd1, &c2, 1);
  if(r!=1) fail("Failed to read from 4\n");
  if(c!=c2)
    fail("Value read from 4 differed from value written to 1\n");

  printf("test symlinks: ok\n");
  // 结束，关闭文件 1 和 4
done:
  close(fd1);
  close(fd2);
}

static void
concur(void)
{
  int pid, i;
  int fd;
  struct stat st;
  int nchild = 2;

  printf("Start: test concurrent symlinks\n");
    
  // 创建文件 z
  fd = open("/testsymlink/z", O_CREATE | O_RDWR);
  if(fd < 0) {
    printf("FAILED: open failed");
    exit(1);
  }
  close(fd);

  // for 循环：创建两个子进程，循环多次给 z 创建软连接 y，或者删除软连接 y
  // 测试么此创建出的 y 是否是一个符号链接
  for(int j = 0; j < nchild; j++) {
    pid = fork();
    if(pid < 0){
      printf("FAILED: fork failed\n");
      exit(1);
    }
    if(pid == 0) {
      int m = 0;
      unsigned int x = (pid ? 1 : 97);
      for(i = 0; i < 100; i++){
        x = x * 1103515245 + 12345;
        if((x % 3) == 0) {
          symlink("/testsymlink/z", "/testsymlink/y");
          if (stat_slink("/testsymlink/y", &st) == 0) {
            m++;
            if(st.type != T_SYMLINK) {
              printf("FAILED: not a symbolic link\n", st.type);
              exit(1);
            }
          }
        } else {
          unlink("/testsymlink/y");
        }
      }
      exit(0);
    }
  }

  // 等待两个子进程结束
  int r;
  for(int j = 0; j < nchild; j++) {
    wait(&r);
    if(r != 0) {
      printf("test concurrent symlinks: failed\n");
      exit(1);
    }
  }
  printf("test concurrent symlinks: ok\n");
}
