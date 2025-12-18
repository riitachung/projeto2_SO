#include "board.h"
#include "game.h"
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <pthread.h>

struct SessionArguments {
   int req_pipe;
   int notif_pipe;
};

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

/* loop
ler pedido cliente
atualizar jogo
enviar updates notif pipe
termina se disconect (2)
*/
void* session_thread (void* arg) {
   struct SessionArguments *args = (struct SessionArguments*) arg;
   int req_pipe = args->req_pipe;
   //int notif_pipe = args->notif_pipe;
   char opcode, command;

   while(1) {
      write(notif_fd, )
      read(req_pipe, &opcode, sizeof(char));
      if(opcode == 3){     // pacman play
         read(req_pipe, &command, sizeof(char));
         
      }
   }
   
}

int main(int argc, char *argv[]) { // PacmanIST levels_dir max_games nome_do_FIFO_de_registo
   if(argc != 4) return -1;
   char *levels_dir = argv[1];
   //int max_games = atoi(argv[2]);
   const char *server_pipe_path = argv[3];

   int server_fd, notif_fd, request_fd, result_main;
   unlink(server_pipe_path);
   open_debug_file("server-debug.log");

   /*-------- CRIA O FIFO DO SERVIDOR ----------*/
   if(mkfifo(server_pipe_path, 0666) < 0) return -1;
   server_fd = open(server_pipe_path, O_RDONLY);
   if(server_fd < 0) return -1;

   char opcode, req_pipe_path[40], notif_pipe_path[40], opcode_result = 1, result = 0;
   read(server_fd, &opcode, sizeof(char));               // lê opcode enviado pelo client

   /*------------CASO DO CONNECT-----------*/
   if(opcode == 1){                                      // se for pedido de inicio de sessao
      
      if(read(server_fd, req_pipe_path, sizeof(req_pipe_path)) != 40) return -1;
      if(read(server_fd, notif_pipe_path, sizeof(notif_pipe_path)) != 40) return -1;
      debug("leu pipes do cliente..\n");

      req_pipe_path[39] = '\0';
      notif_pipe_path[39] = '\0';

      notif_fd = open(notif_pipe_path, O_WRONLY);
      debug("abriu pipe das notificaçõe..\n");

      // se o read passou responde ao cliente, opcode = 1, result = 0
      write(notif_fd, &opcode_result, sizeof(char));
      write(notif_fd, &result, sizeof(char));
      debug("escreveu no pipe das notificações..\n");

      request_fd = open(req_pipe_path, O_RDONLY);
      debug("abriu pipe de requests..\n");

      if(request_fd < 0) {
         close(notif_fd);
      }

      struct SessionArguments *args = malloc(sizeof(struct SessionArguments));
      args->req_pipe = request_fd;
      args->notif_pipe = notif_fd;
      int result_main = start_session(levels_dir);     // TRATAAAAR
      debug("começou sessão..\n");
      pthread_t pid;
      pthread_create(&pid, NULL, session_thread, args);
      pthread_join(pid, NULL);
      free(args);
   }
   close_debug_file();

   return 0;
}
