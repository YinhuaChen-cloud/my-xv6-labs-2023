#include "kernel/types.h"
#include "user/user.h"
#include "kernel/param.h"
#include <stddef.h>

#define ARGV_LEN 64

// 先读取标准输入的每一行，再把每一行添加到我们要执行的命令后边
// 随后执行命令
int
main(int argc, char *argv[])
{
    assert(argc > 1 && argc < MAXARG);

    // 用来存放标准输入流(每行)的缓冲区
    char buf[512];
    // NOTE: 注意：exec 的第二个参数不能是二维数组，因为二维数组实际上是一维数组
    char *final_argv[MAXARG];
    for(int i = 0; i < MAXARG; i++)
        final_argv[i] = NULL;

    // 只要还能从标准输入读取到内容，继续循环
    while(gets(buf, sizeof buf)[0] != '\0') {
        assert(strlen(buf) < sizeof buf);
        // 把末尾换行符去掉，不然名字匹配可能会因为多一个换行符失败
        assert(buf[strlen(buf)-1] == '\n');
        buf[strlen(buf)-1] = '\0';
        // 把 buf 以及以后的内容添加到命令的末尾，随后执行
        // 创建子进程来执行命令
        int pid = fork();
        if (pid < 0) {
            fprintf(2, "error: fork\n");
            exit(1);
        }
        if (pid == 0) { // 子进程
            // 先把原本的参数逐个放进 final_argv
            int i;
            for(i = 0; i+1 < argc; i++) {
                assert(strlen(argv[i+1]) < ARGV_LEN);
                final_argv[i] = (char *)malloc(sizeof(ARGV_LEN));
                strcpy(final_argv[i], argv[i+1]);
            }
            // 再把读入的内容按照空格划分，逐个放入 final_argv
            char *p = buf;
            while(*p) {
                final_argv[i] = (char *)malloc(sizeof(ARGV_LEN));
                int k = 0;
                while(*p && *p != ' ') {
                    final_argv[i][k] = *p;
                    k++;
                    p++;
                    assert(k < ARGV_LEN);
                }
                final_argv[i][k] = '\0';
                i++;
                p++;
            }
            // 执行命令
            exec(final_argv[0], final_argv);
            exit(0);
        }
        // 等待子进程退出
        while(wait(NULL) > 0);

        // 下一轮之前释放内存
        for(int i = 0; i < MAXARG; i++) {
            if(final_argv[i]) {
                free(final_argv[i]);
                final_argv[i] = NULL;
            }
        }
    }

    exit(0);
}
