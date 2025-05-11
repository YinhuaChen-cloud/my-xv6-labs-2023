//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  argint(n, &fd);
  if(fd < 0 || fd >= NOFILE || (f=myproc()->ofile[fd]) == 0)
    return -1;
  if(pfd)
    *pfd = fd;
  if(pf)
    *pf = f;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int
fdalloc(struct file *f)
{
  int fd;
  struct proc *p = myproc();

  for(fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd] == 0){
      p->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

uint64
sys_dup(void)
{
  struct file *f;
  int fd;

  if(argfd(0, 0, &f) < 0)
    return -1;
  if((fd=fdalloc(f)) < 0)
    return -1;
  filedup(f);
  return fd;
}

uint64
sys_read(void)
{
  struct file *f;
  int n;
  uint64 p;

  argaddr(1, &p);
  argint(2, &n);
  if(argfd(0, 0, &f) < 0)
    return -1;
  return fileread(f, p, n);
}

uint64
sys_write(void)
{
  struct file *f;
  int n;
  uint64 p;
  
  argaddr(1, &p);
  argint(2, &n);
  if(argfd(0, 0, &f) < 0)
    return -1;

  return filewrite(f, p, n);
}

uint64
sys_close(void)
{
  int fd;
  struct file *f;

  if(argfd(0, &fd, &f) < 0)
    return -1;
  myproc()->ofile[fd] = 0;
  fileclose(f);
  return 0;
}

uint64
sys_fstat(void)
{
  struct file *f;
  uint64 st; // user pointer to struct stat

  argaddr(1, &st);
  if(argfd(0, 0, &f) < 0)
    return -1;
  return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
uint64
sys_link(void)
{
  char name[DIRSIZ], new[MAXPATH], old[MAXPATH];
  struct inode *dp, *ip;

  if(argstr(0, old, MAXPATH) < 0 || argstr(1, new, MAXPATH) < 0)
    return -1;

  begin_op();
  if((ip = namei(old)) == 0){
    end_op();
    return -1;
  }

  ilock(ip);
  if(ip->type == T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }

  ip->nlink++;
  iupdate(ip);
  iunlock(ip);

  if((dp = nameiparent(new, name)) == 0)
    goto bad;
  ilock(dp);
  if(dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0){
    iunlockput(dp);
    goto bad;
  }
  iunlockput(dp);
  iput(ip);

  end_op();

  return 0;

bad:
  ilock(ip);
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  end_op();
  return -1;
}

// Is the directory dp empty except for "." and ".." ?
static int
isdirempty(struct inode *dp)
{
  int off;
  struct dirent de;

  for(off=2*sizeof(de); off<dp->size; off+=sizeof(de)){
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if(de.inum != 0)
      return 0;
  }
  return 1;
}

uint64
sys_unlink(void)
{
  struct inode *ip, *dp;
  struct dirent de;
  char name[DIRSIZ], path[MAXPATH];
  uint off;

  if(argstr(0, path, MAXPATH) < 0)
    return -1;

  begin_op();
  if((dp = nameiparent(path, name)) == 0){
    end_op();
    return -1;
  }

  ilock(dp);

  // Cannot unlink "." or "..".
  if(namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
    goto bad;

  if((ip = dirlookup(dp, name, &off)) == 0)
    goto bad;
  ilock(ip);

  if(ip->nlink < 1)
    panic("unlink: nlink < 1");
  if(ip->type == T_DIR && !isdirempty(ip)){
    iunlockput(ip);
    goto bad;
  }

  memset(&de, 0, sizeof(de));
  if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");
  if(ip->type == T_DIR){
    dp->nlink--;
    iupdate(dp);
  }
  iunlockput(dp);

  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);

  end_op();

  return 0;

bad:
  iunlockput(dp);
  end_op();
  return -1;
}

static struct inode*
create(char *path, short type, short major, short minor)
{
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if((dp = nameiparent(path, name)) == 0)
    return 0;

  ilock(dp);

  if((ip = dirlookup(dp, name, 0)) != 0){
    iunlockput(dp);
    ilock(ip);
    if(type == T_FILE && (ip->type == T_FILE || ip->type == T_DEVICE))
      return ip;
    iunlockput(ip);
    return 0;
  }

  if((ip = ialloc(dp->dev, type)) == 0){
    iunlockput(dp);
    return 0;
  }

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);

  if(type == T_DIR){  // Create . and .. entries.
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if(dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      goto fail;
  }

  if(dirlink(dp, name, ip->inum) < 0)
    goto fail;

  if(type == T_DIR){
    // now that success is guaranteed:
    dp->nlink++;  // for ".."
    iupdate(dp);
  }

  iunlockput(dp);

  return ip;

 fail:
  // something went wrong. de-allocate ip.
  ip->nlink = 0;
  iupdate(ip);
  iunlockput(ip);
  iunlockput(dp);
  return 0;
}

uint64
sys_open(void)
{
  char path[MAXPATH];
  int fd, omode;
  struct file *f;
  struct inode *ip;
  int n;

  argint(1, &omode);
  if((n = argstr(0, path, MAXPATH)) < 0)
    return -1;

  begin_op();

  if(omode & O_CREATE){
    ip = create(path, T_FILE, 0, 0);
    if(ip == 0){
      end_op();
      return -1;
    }
  } else {
    if((ip = namei(path)) == 0){
      end_op();
      return -1;
    }
    ilock(ip);
    if(ip->type == T_DIR && omode != O_RDONLY){
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  if(ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)){
    iunlockput(ip);
    end_op();
    return -1;
  }

  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
    if(f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }

  if(ip->type == T_DEVICE){
    f->type = FD_DEVICE;
    f->major = ip->major;
  } else {
    f->type = FD_INODE;
    f->off = 0;
  }
  f->ip = ip;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

  if((omode & O_TRUNC) && ip->type == T_FILE){
    itrunc(ip);
  }

  iunlock(ip);
  end_op();

  return fd;
}

uint64
sys_mkdir(void)
{
  char path[MAXPATH];
  struct inode *ip;

  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_mknod(void)
{
  struct inode *ip;
  char path[MAXPATH];
  int major, minor;

  begin_op();
  argint(1, &major);
  argint(2, &minor);
  if((argstr(0, path, MAXPATH)) < 0 ||
     (ip = create(path, T_DEVICE, major, minor)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_chdir(void)
{
  char path[MAXPATH];
  struct inode *ip;
  struct proc *p = myproc();
  
  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);
  if(ip->type != T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  iput(p->cwd);
  end_op();
  p->cwd = ip;
  return 0;
}

uint64
sys_exec(void)
{
  char path[MAXPATH], *argv[MAXARG];
  int i;
  uint64 uargv, uarg;

  argaddr(1, &uargv);
  if(argstr(0, path, MAXPATH) < 0) {
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for(i=0;; i++){
    if(i >= NELEM(argv)){
      goto bad;
    }
    if(fetchaddr(uargv+sizeof(uint64)*i, (uint64*)&uarg) < 0){
      goto bad;
    }
    if(uarg == 0){
      argv[i] = 0;
      break;
    }
    argv[i] = kalloc();
    if(argv[i] == 0)
      goto bad;
    if(fetchstr(uarg, argv[i], PGSIZE) < 0)
      goto bad;
  }

  int ret = exec(path, argv);

  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);

  return ret;

 bad:
  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);
  return -1;
}

uint64
sys_pipe(void)
{
  uint64 fdarray; // user pointer to array of two integers
  struct file *rf, *wf;
  int fd0, fd1;
  struct proc *p = myproc();

  argaddr(0, &fdarray);
  if(pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0){
    if(fd0 >= 0)
      p->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  if(copyout(p->pagetable, fdarray, (char*)&fd0, sizeof(fd0)) < 0 ||
     copyout(p->pagetable, fdarray+sizeof(fd0), (char *)&fd1, sizeof(fd1)) < 0){
    p->ofile[fd0] = 0;
    p->ofile[fd1] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  return 0;
}

// - 在页面错误处理时懒加载页表。也就是说，mmap 不应分配物理内存或读取文件。相反，在 usertrap
//  中（或由 usertrap 调用的页面错误处理代码中）执行此操作，就像写时复制实验一样。懒加载的原因
//  是为了确保对大文件的 mmap 快速，并且对大于物理内存的文件的 mmap 成为可能。

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

// char *p = mmap(0, PGSIZE*2, PROT_READ, MAP_PRIVATE, fd, 0);
uint64
sys_mmap(void)
{
  uint64 addr;
  uint64 length;
  int prot;
  int flags;
  int fd;
  struct file *fp;
  uint64 offset;

  // argaddr(0, &addr); 忽略第一个参数，总是自主选择 addr
  // (使用 p->sz 作为映射的虚拟地址起始)
  struct proc *p = myproc();
  addr = p->sz;
  argaddr(1, &length);
  argint(2, &prot);
  argint(3, &flags);
  argfd(4, &fd, &fp);
  argaddr(5, &offset);

  // 这里要求 mmap 的 length 必须是 PGSIZE 的整数倍
  if(length % PGSIZE != 0)
    panic("mmap: length is not multiple times of PGSIZE");

  // 如果文件是不可写，而 mmap 参数可写，且文件SHARED，则 return -1
  if(!(fp->writable) && (prot & PROT_WRITE) && (flags == MAP_SHARED))
    return -1;

// - 实现 mmap：在进程的地址空间中找到一个未使用的区域来映射文件，并将一个 VMA 添加到进程的映射区域表中。
// VMA 应该包含一个指向被映射文件的 struct file 的指针；mmap 应该增加文件的引用计数，以便在文件关闭时结构
// 不会消失（提示：参见 filedup）。运行 mmaptest：第一个 mmap 应该成功，但是对 mmap-ed 内存的第一次访问将
// 导致页面错误并杀死 mmaptest。
  // 查找一个 VMA
  int i;
  for(i = 0; i < VMA_SIZE; i++) {
    // 查找对应的 VMA
    if(!(p->vmas[i].used)) 
      break;
  }
  if(i >= VMA_SIZE) {
    printf("mmap: not enough VMA\n");
    return -1;
  }
  // mmap 不可能映射到同一个虚拟地址上，因为 addr 使用 p->sz
  p->vmas[i].addr    = addr;
  p->vmas[i].length  = length;
  p->vmas[i].prot    = prot;
  p->vmas[i].flags   = flags;
  p->vmas[i].fp      = fp;
  p->vmas[i].offset  = offset;
  p->vmas[i].used    = 1;
  fp->ref++;

  p->sz += length;

  // mmap 成功时，返回映射内存的用户虚拟地址
  if(addr % PGSIZE != 0)
    panic("mmap: addr %% PGSIZE != 0");
  return addr;
}

static 
uint64 
similarVMA(uint64 addr, uint64 length, VMA vma) 
{
  struct proc *p = myproc();
  int i;
  for(i = 0; i < VMA_SIZE; i++) {
    if(!(p->vmas[i].used))
      break;
  }
  if(i >= VMA_SIZE)
    return -1;
  p->vmas[i].addr    = addr;
  p->vmas[i].length  = length;
  p->vmas[i].prot    = vma.prot;
  p->vmas[i].flags   = vma.flags;
  p->vmas[i].fp      = vma.fp;
  p->vmas[i].offset  = vma.offset;
  p->vmas[i].used    = 1;
  p->vmas[i].fp->ref++;
  return 0;
}

// - 实现 munmap：找到地址范围的 VMA 并取消映射指定的页面（提示：使用 uvmunmap）。
// 如果 munmap 删除了之前 mmap 的所有页面，则应该减少相应 struct file 的引用计数。
// 如果取消映射的页面已被修改并且文件被映射为 MAP_SHARED，则应将页面写回文件。
// 查看 filewrite 以获取灵感。
// - 理想情况下，您的实现只会写回程序实际修改的 MAP_SHARED 页面。RISC-V PTE 中的脏位
// （D）指示页面是否已被写入。但是，mmaptest 不会检查未修改的页面是否没有被写回；因此，
// 您可以不查看 D 位就写回页面。
// - 修改 exit 以取消映射进程的映射区域，就像调用了 munmap 一样。运行 mmaptest；
// mmap_test 应该通过，但可能不是 fork_test。

// int munmap(void *addr, size_t len);
// 注意: 要求能分多次进行 munmap
uint64
sys_munmap(void)
{
  uint64 addr;
  uint64 length;

  argaddr(0, &addr);
  argaddr(1, &length);

  // 这里要求 munmap 的 addr 必须是 PGSIZE 的整数倍
  if(addr % PGSIZE != 0)
    panic("munmap: addr is not multiple times of PGSIZE");

  // 这里要求 munmap 的 length 必须是 PGSIZE 的整数倍
  if(length % PGSIZE != 0)
    panic("munmap: length is not multiple times of PGSIZE");

  struct proc *p = myproc();
  int i;
  for(i = 0; i < VMA_SIZE; i++) {
    // 查找对应的 VMA
    if(p->vmas[i].used && addr >= p->vmas[i].addr && addr < p->vmas[i].addr + p->vmas[i].length) {
      // 如果 munmap 的参数非法，那么 return -1
      if(addr + length > p->vmas[i].addr + p->vmas[i].length) {
        printf("munmap: illegal arguments\n");
        return -1;
      }
      // 如果 flags 属于 SHRAED，释放后要写回文件
      // int filewrite(struct file *f, uint64 addr, int n)
      if(p->vmas[i].flags == MAP_SHARED && (p->vmas[i].prot & PROT_WRITE) && filewrite(p->vmas[i].fp, addr, length) < 0) {
        printf("munmap: filewrite failure\n");
        return -1;
      }

      // 如果 munmap 参数只有部分吻合，则要释放原本的 VMA，创建新的 VMA
      // 如果左边没有紧邻
      if(addr > p->vmas[i].addr) {
        if(similarVMA(p->vmas[i].addr, addr - p->vmas[i].addr, p->vmas[i]) < 0) {
          printf("munmap: similarVMA1 failure\n");
          return -1;
        }
      }
      // 如果右边没有紧邻
      if(addr + length < p->vmas[i].addr + p->vmas[i].length) {
        if(similarVMA(addr + length, p->vmas[i].addr + p->vmas[i].length - addr - length, p->vmas[i]) < 0){
          printf("munmap: similarVMA2 failure\n");
          return -1;
        }
      }
      // 上面的情况都要释放 VMA，这里直接释放即可
      p->vmas[i].used = 0;
      p->vmas[i].fp->ref--;
      uvmunmap(p->pagetable, addr, length / PGSIZE, 1);
      // 已经释放 VMA, 可以 break
      break;
    }
  }

  if(i >= VMA_SIZE) {
    printf("munmap: VMA addr %p not found\n", addr);
    return -1;
  }

  return 0;
}

