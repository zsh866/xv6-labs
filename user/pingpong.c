#include "kernel/types.h"
#include "user/user.h"

#define RD  0
#define WD  1

int main(int argc, char const *argv[]) {
    char buf = 'p';

    int fd_ptc[2]; // parent -> child
    int fd_ctp[2]; // child -> parent
    pipe(fd_ptc);
    pipe(fd_ctp);

    int pid = fork();
    int exit_status = 0;

    if (pid == 0) { // child
        close(fd_ptc[WD]); // child 不能写 p->c
        close(fd_ctp[RD]); // child 不能读 c->p

        if (read(fd_ptc[RD], &buf, sizeof(char)) != sizeof(char)) {
            fprintf(2, "child read error\n");
            exit_status = 1;
        } else {
            fprintf(1, "%d: received ping\n", getpid());
        }

        if (write(fd_ctp[WD], &buf, sizeof(char)) != sizeof(char)) {
            fprintf(2, "child write error\n");
            exit_status = 1;
        }

        close(fd_ptc[RD]);
        close(fd_ctp[WD]);
        exit(exit_status);
    } else if (pid > 0) { // parent
        close(fd_ptc[RD]); // parent 不能读 p->c
        close(fd_ctp[WD]); // parent 不能写 c->p

        if (write(fd_ptc[WD], &buf, sizeof(char)) != sizeof(char)) {
            fprintf(2, "parent write error\n");
            exit_status = 1;
        }

        if (read(fd_ctp[RD], &buf, sizeof(char)) != sizeof(char)) {
            fprintf(2, "parent read error\n");
            exit_status = 1;
        } else {
            fprintf(1, "%d: received pong\n", getpid());
        }

        close(fd_ptc[WD]);
        close(fd_ctp[RD]);

        exit(exit_status);
    } else {
        fprintf(2, "fork error!\n");
        exit(1);
    }
}
