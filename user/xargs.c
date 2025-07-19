#include "kernel/types.h"
#include "user/user.h"
#include "kernel/param.h"

#define MAXLINE 1024

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(2, "Usage: xargs command [args...]\n");
        exit(1);
    }

    char *base_argv[MAXARG];
    int i;
    for (i = 1; i < argc && i < MAXARG; i++) {
        base_argv[i - 1] = argv[i];
    }
    int base_argc = i - 1;

    char buf[MAXLINE];
    int n = 0;
    char c;
    while (read(0, &c, 1) == 1) {
        if (c == '\n') {
            buf[n] = 0;

            // 构造参数列表
            char *full_argv[MAXARG];
            int arg_index = 0;

            // 复制基础参数
            for (int j = 0; j < base_argc; j++) {
                full_argv[arg_index++] = base_argv[j];
            }

            // 按空格分词添加输入参数
            char *p = buf;
            while (*p) {
                while (*p == ' ') p++;  // 跳过前导空格
                if (*p == 0) break;
                full_argv[arg_index++] = p;
                while (*p && *p != ' ') p++;  // 移动到下一个空格或结束
                if (*p) *p++ = 0;  // 终结当前单词
            }

            full_argv[arg_index] = 0;  // NULL 结尾

            int pid = fork();
            if (pid < 0) {
                fprintf(2, "fork failed\n");
                exit(1);
            } else if (pid == 0) {
                exec(full_argv[0], full_argv);
                fprintf(2, "exec failed: %s\n", full_argv[0]);
                exit(1);
            } else {
                wait(0);
            }

            n = 0;  // 重置缓冲区位置
        } else {
            if (n < MAXLINE - 1) {
                buf[n++] = c;
            }
        }
    }
    exit(0);
}