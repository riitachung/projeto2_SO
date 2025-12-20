/*
gcc server.c game.c board.c parser.c display.c -o 
PacmanIST -Wall -Wextra -pthread -I ../include -lncursesw
*/


#include "api.h"
#include "protocol.h"
#include "debug.h"

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <stdlib.h>

struct Session {
  int id;
  int req_pipe;
  int notif_pipe;
  char req_pipe_path[MAX_PIPE_PATH_LENGTH + 1];
  char notif_pipe_path[MAX_PIPE_PATH_LENGTH + 1];
};

extern Board board;
static struct Session session = {.id = -1};

int pacman_connect(char const *req_pipe_path, char const *notif_pipe_path, char const *server_pipe_path) {

  unlink(req_pipe_path);                                                // remove possíveis pipes criados anteriormente
  unlink(notif_pipe_path);

  int server_fd, notif_fd, request_fd;
  char op_code = 1, opcode_notif, result, request_buffer[40] = {0}, notif_buffer[40] = {0};

  /*------- CRIAÇÃO DOS FIFOS DO CLIENTE -------*/
  if(mkfifo(req_pipe_path, 0666) < 0) exit(1);                          // cliente manda comandos ao serv
  if(mkfifo(notif_pipe_path, 0666) < 0) exit(1);                        // servidor manda updates do board ao cliente (0666- R/W)

  /*----- ABRE O FIFO DE PEDIDO DE SESSÃO ------*/
  if((server_fd = open(server_pipe_path, O_WRONLY)) < 0) exit(1);       
  write(server_fd, &op_code, 1);                                        // mensagem para o pipe de registo do serv (opcode->1, char[40])

  strncpy(request_buffer, req_pipe_path, sizeof(request_buffer) - 1);   // cria um buffer dos nomes dos paths dos pipes de pedidos e de notificações
  strncpy(notif_buffer, notif_pipe_path, sizeof(notif_buffer) - 1);

  /*----- PASSAGEM DOS FIFOS DO CLIENTE PARA O SERVIDOR-----*/  
  write(server_fd, request_buffer, sizeof(request_buffer));
  write(server_fd, notif_buffer, sizeof(notif_buffer));
  close(server_fd);
  if((notif_fd = open(notif_pipe_path, O_RDONLY)) < 0) exit (1);        // debloqueia fifo de notificações do servidor

  /*---------- LÊ RESPOSTA DO SERVER ----------*/  
  if(read(notif_fd, &opcode_notif, sizeof(char)) <= 0 || (opcode_notif != 1)) exit(1);                         
  if(read(notif_fd, &result, sizeof(char)) <= 0) exit(1);

  if(result != 0) {
    fprintf(stderr, "Servidor não aceitou a conexão\n");                // resultado tem de ser 0 e opcode retornado tem de ser 1
    return 1;
  }

  /*----------- ABRIR FIFO PEDIDOS ------------*/
  request_fd = open(req_pipe_path, O_WRONLY);

  if(request_fd < 0) exit(1);

  /*--------- GUARDAR DADOS DA SESSÃO ---------*/
  session.req_pipe = request_fd;
  session.notif_pipe = notif_fd;

  strncpy(session.req_pipe_path, req_pipe_path, MAX_PIPE_PATH_LENGTH);
  strncpy(session.notif_pipe_path, notif_pipe_path, MAX_PIPE_PATH_LENGTH);

  return 0;
}

// recebe o command do client_main
int pacman_play(char command) {
  char opcode_play = 3;
  if(session.req_pipe < 0) return -1;
  if(write(session.req_pipe, &opcode_play, sizeof(char)) != 1) return -1;  
  if(write(session.req_pipe, &command, sizeof(char)) != 1) return -1;
  return 0;
}

int pacman_disconnect() {
  // TODO - implement me
  char opcode_disconnect = 2;
  if(session.req_pipe < 0) return -1;
  if(write(session.req_pipe, &opcode_disconnect, sizeof(char)) != 1) return -1; 

  /*------ FECHA OS PIPES DA SESSÃO ------*/
  close(session.req_pipe);
  close(session.notif_pipe);

  /*----- REMOVE OS PIPES DA SESSÃO -----*/
  unlink(session.req_pipe_path);
  unlink(session.notif_pipe_path);
  
  /*----- REEINICIAR A SESSÃO -----*/
  session.req_pipe = -1;
  session.notif_pipe = -1;
  strcpy(session.req_pipe_path, "");
  strcpy(session.notif_pipe_path, "");

  return 0;
}

Board receive_board_update(void) {
  char opcode;
  //Board board = {0};
  //int width, height, tempo, victory, game_over, accumulated_points;




  read(session.notif_pipe, &opcode, sizeof(char));    // retorna ou recebe?
  
  read(session.notif_pipe, &board.width, sizeof(int));
  read(session.notif_pipe, &board.height, sizeof(int));
  read(session.notif_pipe, &board.tempo, sizeof(int));
  read(session.notif_pipe, &board.victory, sizeof(int));
  read(session.notif_pipe, &board.game_over, sizeof(int));
  read(session.notif_pipe, &board.accumulated_points, sizeof(int));
  if(board.data == NULL)
    board.data = malloc(board.width * board.height);
  else 
    board.data = realloc(board.data, board.width * board.height);
  read(session.notif_pipe, board.data, (board.width * board.height));

  return board;
}