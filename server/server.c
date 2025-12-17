
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <stdlib.h>

/*
- aceita um unico cliente
- lê connect e envia resposta
- lê comandos do play e atualiza o tabuleiro
- envia updates dotabuleiro
- lida com disconect e termina sessao
SERVER
- criar fifo registo
- espera o conect
- abre pipes do cliente
- envia pipe notificacoes q o connect funcionou
- carrega o nivel e inicia o jogo
LOOP:
- le play, aplica jogada (envia update, thread separada)
- no caso de disconect liberta recursos e termina

COMO PROCEDER:
1. ler o pedido do pacman_connect:
   - abrir o fifo de registo
   - ler opcode + 2 paths (req_pipe, notif_pipe)

2. responder ao cliente:
   - abrir fifo notificacoes
   - mandar OP_CODE=1, result=0

3. abrir fifo de pedidos do cliente 

4. criar thread de sessão:
   (não atualiza tabuleiro; lê periodicamente e envia para cliente via notif)

5. criar thread para comandos:
    le comandos do fifo de pedidos do cliente
 *aplica move_pacman diretamente no board
*/

int main(int argc, char *argv[]) { // PacmanIST levels_dir max_games nome_do_FIFO_de_registo
   if(argc != 4) return -1;
   char *levels_dir = argv[1];
   int max_games = atoi(argv[2]);
   const char *server_pipe_path = argv[3];

   int server_fd, notif_fd, request_fd;
   unlink(server_pipe_path);

   // cria fifo do seguidor
   if(mkfifo(server_pipe_path, 0666) < 0) return -1;
   server_fd = open(server_pipe_path, O_RDONLY);
   if(server_fd < 0) return -1;

   printf("criei fifo\n");
   while(1){
      char opcode, req_pipe_path[40], notif_pipe_path[40], opcode_result = 1, result = 0;
      printf("entrei while antes\n");
   
      if(read(server_fd, &opcode, sizeof(char)) <= 0) continue;
      printf("entrei while depois\n");
      printf("%d\n", opcode);
      if(opcode == 1){
         
         if(read(server_fd, req_pipe_path, sizeof(req_pipe_path)) != 40) continue;
         if(read(server_fd, notif_pipe_path, sizeof(notif_pipe_path)) != 40) continue;
         req_pipe_path[39] = '\0';
         notif_pipe_path[39] = '\0';
         printf("antes open\n");
         notif_fd = open(notif_pipe_path, O_WRONLY);
         if(notif_fd < 0) continue;
         printf("apois notificacoes open\n");
         request_fd = open(req_pipe_path, O_RDONLY);
         printf("apos open\n");

         if(request_fd < 0) {
            close(notif_fd);
            continue;
         }
         printf("antes do conectei\n");
         // se o read passou responde ao cliente, opcode = 1, result = 0
         write(notif_fd, &opcode_result, sizeof(char));
         write(notif_fd, &result, sizeof(char));
         printf("CONECTOU CARALHOOOO\n");
      }
   }
}
