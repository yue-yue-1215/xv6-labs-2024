#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
    int parent_to_child[2], child_to_parent[2];
    char buffer = 'p';  // 传递的字节内容，可以是任意值，测试用

    pipe(parent_to_child);
    pipe(child_to_parent);

    int pid = fork();
    if (pid < 0) {
        printf("fork error\n");
        exit(1);
    }

    if (pid == 0) {  // 子进程
        close(parent_to_child[1]);  // 关闭写端
        close(child_to_parent[0]);  // 关闭读端

        // 读父进程传来的字节
        if (read(parent_to_child[0], &buffer, 1) != 1) {
            printf("child read error\n");
            exit(1);
        }
        printf("%d: received ping\n", getpid());

        // 向父进程写回字节
        if (write(child_to_parent[1], &buffer, 1) != 1) {
            printf("child write error\n");
            exit(1);
        }

        close(parent_to_child[0]);
        close(child_to_parent[1]);
        exit(0);

    } else {  // 父进程
        close(parent_to_child[0]);  // 关闭读端
        close(child_to_parent[1]);  // 关闭写端

        // 向子进程写字节
        if (write(parent_to_child[1], &buffer, 1) != 1) {
            printf("father write error\n");
            exit(1);
        }

        // 读子进程写回的字节
        if (read(child_to_parent[0], &buffer, 1) != 1) {
            printf("father read error\n");
            exit(1);
        }
        printf("%d: received pong\n", getpid());

        close(parent_to_child[1]);
        close(child_to_parent[0]);
        exit(0);
    }
}