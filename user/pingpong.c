#include "kernel/types.h" // user.h 自定义了数据类型
#include "user/user.h"

int main() {
    int ftoc[2]; // 管道1：父进程到子进程
    int ctof[2]; // 管道2：子进程到父进程
    // 0 是读端，1是写端

    // 创建管道
    if (pipe(ftoc) == -1 || pipe(ctof) == -1) {
        fprintf(2, "error: create pipe\n");
        exit(1);
    }

    int pid = fork();
    if (pid < 0) {
        fprintf(2, "error: fork\n");
        exit(1);
    }

    if (pid == 0) { // 子进程

        // 子进程 pid
        int child_pid = getpid();

        // 从父进程读取一个字节
        char receivemsg = '\0';
        read(ftoc[0], &receivemsg, 1);

        printf("%d: received ping\n", child_pid);
        printf("%d: received %c\n", child_pid, receivemsg);

        // 向父进程发送一个字节
        char sendmsg = 'B'; // 发送的字节
        write(ctof[1], &sendmsg, 1);

        close(ftoc[0]); // 关闭父到子管道的读端
        close(ftoc[1]); // 关闭父到子管道的写端
        close(ctof[0]); // 关闭子到父管道的读端
        close(ctof[1]); // 关闭子到父管道的写端
        exit(0);
    } else { // 父进程

        // 父进程 pid
        int father_pid = getpid();

        // 向子进程发送一个字节
        char sendmsg = 'A'; // 发送的字节
        write(ftoc[1], &sendmsg, 1);

        // 从子进程读取一个字节
        char receivemsg = '\0';
        read(ctof[0], &receivemsg, 1);
        printf("%d: received pong\n", father_pid);
        printf("%d: received %c\n", father_pid, receivemsg);

        close(ftoc[0]); // 关闭父到子管道的读端
        close(ftoc[1]); // 关闭父到子管道的写端
        close(ctof[0]); // 关闭子到父管道的读端
        close(ctof[1]); // 关闭子到父管道的写端
        exit(0);
    }

    exit(0);
}
