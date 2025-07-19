#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

void find(char *path, char *target) {   //在目录path下查找文件target
    char buf[512], new_path[512];
    int fd;
    struct dirent de;
    struct stat st;

    if ((fd = open(path, 0)) < 0) {
        fprintf(2, "find: 无法打开！ %s\n", path);
        return;
    }

    if (fstat(fd, &st) < 0) {
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        return;
    }

    // 路径拼接
    if (strlen(path) + 1 + DIRSIZ + 1 > sizeof(buf)) {
        fprintf(2, "find: 路径太长！\n");
        close(fd);
        return;
    }

    strcpy(buf, path);
    char *p = buf + strlen(buf);
    *p++ = '/';

    // 遍历目录内容
    while (read(fd, &de, sizeof(de)) == sizeof(de)) {
        if (de.inum == 0)
            continue;
        
        // 跳过 "." 和 ".."
        if (!strcmp(de.name, ".") || !strcmp(de.name, ".."))
            continue;

        // 生成新路径
        memmove(p, de.name, DIRSIZ);
        p[DIRSIZ] = 0;

        // 获取文件状态
        if (stat(buf, &st) < 0) {
            printf("find: cannot stat %s\n", buf);
            continue;
        }

        // 判断文件类型
        switch (st.type) {
            case T_FILE:
                if (!strcmp(de.name, target)) {
                    printf("%s\n", buf);
                }
                break;
            case T_DIR:
                // 递归查找子目录
                find(buf, target);
                break;
        }
    }

    close(fd);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(2, "参数错误！使用方法：find <目录> <文件名>\n");
        exit(1);
    }

    find(argv[1], argv[2]);
    exit(0);
}