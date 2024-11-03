#include "kernel/types.h" // user.h 自定义了数据类型
#include "user/user.h"

// 1. 创建一个子进程
// 2. 接受父进程的数据，打印第一个数据
// 3. 把剩下所有数据传给子进程
// 4. 让子进程做同样的事情
void prime_process(int father_to_me[2]) {
    // 创建管道
    int me_to_child[2];
    if (pipe(me_to_child) == -1) {
        fprintf(2, "error: create pipe\n");
        exit(1);
    }

    // 从父进程读入一个数字，并打印
    int firstint = 0;
    if(read(father_to_me[0], &firstint, sizeof(int)) == 0) 
        return; // 如果这一步失败，直接返回
    printf("prime %d\n", firstint);
    // 把剩余所有数字写入 me_to_child 的写端
    // 当 father_to_me[1] 被关闭时，会返回 0
    int receiveint = 0;
    while(read(father_to_me[0], &receiveint, sizeof(int)) != 0) { 
        // 判断是否能被 第一个 接收到的数字整除，如果不能，传输给子进程
        if(receiveint % firstint != 0)
            write(me_to_child[1], &receiveint, sizeof(int));
    }
    close(father_to_me[0]); // 关闭来自父亲的读端
    close(me_to_child[1]); // 关闭向儿子的写端

    // 创建子进程
    int pid = fork();
    if (pid < 0) {
        fprintf(2, "error: fork\n");
        exit(1);
    }
    if (pid == 0) { // 子进程
        prime_process(me_to_child);
        exit(0);
    }

    // 根据谷歌，大部分使用 fork 的程序都要等待子进程结束自己再结束
    // 否则可能引发一些意料之外的错误
    while(wait(NULL) > 0);
}

int main() {
    // 创建管道
    int me_to_child[2];
    if (pipe(me_to_child) == -1) {
        fprintf(2, "error: create pipe\n");
        exit(1);
    }

    // 把所有数字写入 me_to_child 的写端
    for(int i = 2; i <= 35; i++) {
        write(me_to_child[1], &i, sizeof(int));
    }
    close(me_to_child[1]); // 关闭向儿子的写端

    // 创建子进程
    int pid = fork();
    if (pid < 0) {
        fprintf(2, "error: fork\n");
        exit(1);
    }
    if (pid == 0) { // 子进程
        prime_process(me_to_child);
        exit(0);
    }

    // 根据谷歌，大部分使用 fork 的程序都要等待子进程结束自己再结束
    // 否则可能引发一些意料之外的错误
    while(wait(NULL) > 0);
    exit(0);
}
