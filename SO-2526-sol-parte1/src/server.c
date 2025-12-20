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
   int client_id;
   int req_pipe;
   int notif_pipe;
   board_t board;
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

void* update_board_thread (void* arg) {
   debug("\nupdate_board_thread:\n");
   struct SessionArguments *args = (struct SessionArguments*) arg;
   int notif_pipe = args->notif_pipe;
   //board_t *board = &args->board;
   board_t temp;

   while(1){
      debug("  Entrada no loop onde vão ser escritas as infos do Board\n");
      get_board(&temp);
      char opcode = 4;

      // se o board ainda não foi inicializado no load_level, não escrever nada (porque, se escrevesse, iria ser lixo)
      if (temp.width == 0 && temp.height == 0 && temp.board == NULL) {
         debug("  Board ainda não está pronto, aguardando...\n");
         usleep(100000);
         continue; 
      }

      write(notif_pipe, &opcode, sizeof(char));
      write(notif_pipe, &temp.width, sizeof(int));
      write(notif_pipe, &temp.height, sizeof(int));
      write(notif_pipe, &temp.tempo, sizeof(int));
      write(notif_pipe, &victory, sizeof(int));                            // victory
      write(notif_pipe, &game_over, sizeof(int));                          // game_over
      write(notif_pipe, &temp.pacmans[0].points, sizeof(int));             // accumulated_points
      char *data = malloc(temp.width * temp.height);

      // TO-DO nao percebeo porque nao esta a dar no server-debug.log
      debug("  As infos do tabuleiro foram escritas no pipe\n");
      debug("  DIM %d %d, points = %d\n", temp.width, temp.height, temp.pacmans[0].points);
      debug("  victory = %d, game_over = %d\n", victory, game_over);

      for (int i = 0; i < temp.width * temp.height; i++) {
         // TO-DO perceber pq e que pacman n aparece em content
         if(/*temp.board[i].content == 'C' ||*/ temp.board[i].content == 'M'){
            data[i] = 'M';
         }else if (temp.board[i].content == 'W') {
               data[i] = '#';
         } else{
            if(temp.board[i].has_dot == 1)
               data[i] = '.';
            else if (temp.board[i].has_portal == 1)
               data[i] = '@';
            else data[i] = ' ';
         }
         int x = temp.pacmans[0].pos_x;
         int y = temp.pacmans[0].pos_y;

         int idx = y * temp.width + x;
         if (idx >= 0 && idx < temp.width * temp.height) {
            data[idx] = 'C';
         }

      }
      write(notif_pipe, data, (temp.width * temp.height));           // data 
      free(data);
      debug("  Board enviado para o cliente\n");
      
      if (victory == 1 || game_over == 1) {
         debug("O jogo terminou\n");
         break;                                      // sai do loop quando o jogo acaba para não atualizar mais o board
      }
      
      usleep(200000); 
      // TO-DO VER DATA
   }
   return NULL;
}


void* session_thread (void* arg) {
   struct SessionArguments *args = (struct SessionArguments*) arg;
   int req_pipe = args->req_pipe;
   //int notif_pipe = args->notif_pipe;
   char opcode, command;

   while(1) {
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
   debug("Servidor criado\n");

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

      req_pipe_path[39] = '\0';
      notif_pipe_path[39] = '\0';

      notif_fd = open(notif_pipe_path, O_WRONLY);

      // se o read passou responde ao cliente, opcode = 1, result = 0
      write(notif_fd, &opcode_result, sizeof(char));
      write(notif_fd, &result, sizeof(char));
      debug("Cliente conectado\n");
      
      request_fd = open(req_pipe_path, O_RDONLY);

      if(request_fd < 0) {
         close(notif_fd);
      }

      struct SessionArguments *args = malloc(sizeof(struct SessionArguments));
      args->req_pipe = request_fd;
      args->notif_pipe = notif_fd;

      pthread_t tid_session, tid_board;
      pthread_create(&tid_session, NULL, session_thread, args);
      pthread_create(&tid_board, NULL, update_board_thread, args);
      debug("Começo do jogo com a criaçõa das threads - update_board_thread e session_thread");
      result_main = start_session(levels_dir);     // TRATAAAAR
      printf("%d\n", result_main);
      pthread_join(tid_session, NULL);
      pthread_join(tid_board, NULL);
      free(args);

   }
   close_debug_file();

   return 0;
}
