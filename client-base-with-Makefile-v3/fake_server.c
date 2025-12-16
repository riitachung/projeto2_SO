#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <stdlib.h>


int main() {
    const char *reg_pipe = "/tmp/server_register_pipe";

    unlink(reg_pipe);
    mkfifo(reg_pipe, 0666);

    printf("[SERVER] à espera de CONNECT…\n");

    int fd = open(reg_pipe, O_RDONLY);
    if (fd < 0) { perror("open"); exit(1); }

    char op;
    char req_pipe[40];
    char notif_pipe[40];

    read(fd, &op, 1);
    read(fd, req_pipe, 40);
    read(fd, notif_pipe, 40);

    printf("[SERVER] Recebi OP=%d\n", op);
    printf("[SERVER] req_pipe = %s\n", req_pipe);
    printf("[SERVER] notif_pipe = %s\n", notif_pipe);

    // abrir o pipe notif do cliente (para escrever resposta)
    int notif_fd = open(notif_pipe, O_WRONLY);

    // enviar resposta: OP_CODE=1, result=0 (OK)
    char resp_op = 1;
    char result = 0;

    write(notif_fd, &resp_op, 1);
    write(notif_fd, &result, 1);

    printf("[SERVER] Resposta enviada.\n");
    // ----- LER COMANDOS DO CLIENTE -----

    printf("[SERVER] À espera de comandos…\n");

    // abrir req_pipe do cliente para leitura
    int req_fd = open(req_pipe, O_RDONLY);
    if (req_fd < 0) { perror("open req_pipe"); exit(1); }

    while (1) {
        char op;
        char cmd;

        int n = read(req_fd, &op, 1);
        if (n <= 0) break;

        if (op == 3) {
            read(req_fd, &cmd, 1);
            printf("[SERVER] Recebi comando: %c\n", cmd);
        }
    }

    close(notif_fd);
    close(fd);
}
