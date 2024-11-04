#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"

#define BUF_LEN 512

// p - buf = strlen(buf)
void
find(char *buf, char *p, char *targetname)
{
    int fd;
    struct dirent de;
    struct stat st;

    // 判断是否 buf 够长
    if((p - buf) + 1 + DIRSIZ + 1 > BUF_LEN){
        fprintf(2, "find: path too long\n");
        exit(1);
    }
    // 打开当前目录
    if((fd = open(buf, O_RDONLY)) < 0){
        fprintf(2, "find: cannot open %s\n", buf);
        return;
    }
    // 解析当前目录
    if(fstat(fd, &st) < 0){
        fprintf(2, "find: cannot stat %s\n", buf);
        close(fd);
        return;
    }

    assert(st.type == T_DIR);

    // 读取目录每一项
    while(read(fd, &de, sizeof(de)) == sizeof(de)){
        // 节点为 0，没有文件没有目录没有设备，skip
        if(de.inum == 0)
            continue;
        // 有内容，过滤掉 "." 和 ".." 这两个目录
        if(0 == strcmp(".", de.name) || 0 == strcmp("..", de.name))
            continue;
        // 有内容，判断是文件/设备/目录
        // 把名字复制进 buf，以 '\0' 结尾
        *p++ = '/';
        memmove(p, de.name, DIRSIZ);
        p[DIRSIZ] = 0;

        // 看看这玩意儿是文件还是目录
        if(stat(buf, &st) < 0){
            // 若不能解析，直接跳过
            fprintf(2, "find: cannot stat %s\n", buf);
            goto nextloop;
        }
        // 文件/设备，直接判断名字是否匹配，若匹配，则打印
        if(st.type == T_FILE || st.type == T_DEVICE) {
            // 有内容，判断这个 entry 的名字是否是 targetname
            if(0 == strcmp(de.name, targetname)) 
                printf("%s\n", buf);
        }
        else {
            // 是目录，看名字是否匹配，若匹配，打印
            // 无论是否匹配都要进行递归
            assert(st.type == T_DIR);
            if(0 == strcmp(de.name, targetname)) 
                printf("%s\n", buf);
            find(buf, p + strlen(p), targetname);
        }
    nextloop:
        // 读取当前目录的下一个文件之前，重置 p 指针
        *--p = '\0';
    }

    close(fd);
}

// 仅支持 find [dirname] [targetname] 这种用法
// 仅支持精确匹配
int
main(int argc, char *argv[])
{
    char buf[BUF_LEN], *p;

    if(argc != 3){
        fprintf(2, "usage: find [dirname] [targetname]\n");
        exit(1);
    }

    // targetname 不能是 "." 和 ".."
    assert(0 != strcmp(".", argv[2]) && 0 != strcmp("..", argv[2]));

    // 先把 dirname 赋值进 buf
    strcpy(buf, argv[1]);
    p = buf+strlen(buf);
    if((p - buf) + 1 + DIRSIZ + 1 > BUF_LEN){
        fprintf(2, "find: dirname too long\n");
        exit(1);
    }
    // 随后调用 find 打印所有匹配 targetname 的项
    find(buf, p, argv[2]);
    exit(0);
}
