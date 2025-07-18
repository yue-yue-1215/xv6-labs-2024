#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// 递归处理 primes
void _primes(int fd) {
    int prime;

    if (read(fd, &prime, sizeof(prime)) == 0) {
        close(fd);
        exit(0);
    }

    printf("prime %d\n", prime);

    int p[2];
    pipe(p);

    int pid = fork();
    if (pid < 0) {
        fprintf(2, "fork error\n");
        exit(1);
    }

    if (pid == 0) {
        close(p[1]);
        close(fd);
        void (*func_ptr)(int) = _primes;
        func_ptr(p[0]);
    } else {
        close(p[0]);
        int num;
        while (read(fd, &num, sizeof(num)) > 0) {
            if (num % prime != 0) {
                write(p[1], &num, sizeof(num));
            }
        }
        close(fd);
        close(p[1]);
        wait(0);
        exit(0);
    }
}

void primes(int fd) {
    _primes(fd);
}

int main() {
    int p[2];
    pipe(p);

    int pid = fork();
    if (pid < 0) {
        fprintf(2, "fork error\n");
        exit(1);
    }

    if (pid == 0) {
        close(p[1]);
        primes(p[0]);
    } else {
        close(p[0]);
        for (int i = 2; i <= 269; i++) {
            write(p[1], &i, sizeof(i));
        }
        close(p[1]);
        wait(0);
        exit(0);
    }
}
