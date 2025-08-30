#include "kernel/types.h"
#include "user/user.h"

#define RD 0
#define WR 1

const uint INT_LEN = sizeof(int);

// 从管道读第一个数
int lpipe_first_data(int lpipe[2], int *dst) {
  if (read(lpipe[RD], dst, INT_LEN) == INT_LEN) {
    printf("prime %d\n", *dst);
    return 0;
  }
  return -1;
}

// 过滤掉能被 first 整除的数
void transmit_data(int lpipe[2], int rpipe[2], int first) {
  int data;
  while (read(lpipe[RD], &data, INT_LEN) == INT_LEN) {
    if (data % first != 0) {
      write(rpipe[WR], &data, INT_LEN);
    }
  }
  close(lpipe[RD]);
  close(rpipe[WR]);
}

// 递归筛质数
void primes(int lpipe[2]) {
  close(lpipe[WR]);   // 读端只读
  int first;

  if (lpipe_first_data(lpipe, &first) == 0) {
    int p[2];
    pipe(p);

    if (fork() == 0) {
      // 子进程递归处理
      close(lpipe[RD]);
      primes(p);
    } else {
      // 父进程传数据给子进程
      transmit_data(lpipe, p, first);
      close(p[RD]);
      wait(0);
    }
  }
  exit(0);
}

int main(int argc, char const *argv[]) {
  int p[2];
  pipe(p);

  // 将 2..35 写入管道
  for (int i = 2; i <= 35; i++) {
    write(p[WR], &i, INT_LEN);
  }

  if (fork() == 0) {
    // 子进程负责递归处理
    close(p[WR]);
    primes(p);
  } else {
    // 父进程关闭管道，等待子进程
    close(p[WR]);
    close(p[RD]);
    wait(0);
  }
  exit(0);
}
