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

/// @brief função que conecta o cliente ao servidor
/// @param req_pipe_path 
/// @param notif_pipe_path 
/// @param server_pipe_path 
/// @return 0 se sucesso, 1 se erro
int pacman_connect(char const *req_pipe_path, char const *notif_pipe_path, char const *server_pipe_path) {

  unlink(req_pipe_path);                                                // remove possíveis pipes criados anteriormente
  unlink(notif_pipe_path);

  int server_fd, notif_fd, request_fd;
  char opcode_connect = 1, opcode_notif, result, request_buffer[40] = {0}, notif_buffer[40] = {0};

  /*------- CRIAÇÃO DOS FIFOS DO CLIENTE -------*/
  if(mkfifo(req_pipe_path, 0666) < 0) return 1;                         // fifo de pedidos de comandos ao servidor
  if(mkfifo(notif_pipe_path, 0666) < 0) return 1;                       // fifo de notificações que recebe updates do tabuleiro

  /*----- ABRE O FIFO DE PEDIDO DE SESSÃO ------*/
  if((server_fd = open(server_pipe_path, O_WRONLY)) < 0) return 1;       
  if(write(server_fd, &opcode_connect, sizeof(char)) != 1) return 1;    // escreve o opcode de pedido de sessão (1) no fifo de registo
                                    
  strncpy(request_buffer, req_pipe_path, sizeof(request_buffer) - 1);   // cria um buffer com os 40 caracteres dos nomes dos paths dos pipes
  strncpy(notif_buffer, notif_pipe_path, sizeof(notif_buffer) - 1);

  /*----- PASSAGEM DOS FIFOS DO CLIENTE PARA O SERVIDOR-----*/  
  if(write(server_fd, request_buffer, sizeof(request_buffer)) != 40 ||
    write(server_fd, notif_buffer, sizeof(notif_buffer)) != 40) return 1;

  close(server_fd);
  if((notif_fd = open(notif_pipe_path, O_RDONLY)) < 0) return 1;        // debloqueia fifo de notificações do servidor

  /*---------- LÊ RESPOSTA DO SERVER ----------*/  
  if(read(notif_fd, &opcode_notif, sizeof(char)) != 1 || (opcode_notif != 1)) return 1;                         
  if(read(notif_fd, &result, sizeof(char)) != 1) return 1;
  if(result != 0) return 1;
  
  /*----------- ABRIR FIFO PEDIDOS ------------*/
  if((request_fd = open(req_pipe_path, O_WRONLY)) < 0) return 1;

  /*--------- GUARDAR DADOS DA SESSÃO ---------*/
  session.req_pipe = request_fd;
  session.notif_pipe = notif_fd;

  strncpy(session.req_pipe_path, req_pipe_path, MAX_PIPE_PATH_LENGTH);
  strncpy(session.notif_pipe_path, notif_pipe_path, MAX_PIPE_PATH_LENGTH);

  return 0;
}


/// @brief função que envia o comando desejado para o pacman para o servidor
/// @param command 
/// @return 0 se sucesso, -1 se erro
int pacman_play(char command) {
  char opcode_play = 3;
  if(write(session.req_pipe, &opcode_play, sizeof(char)) != 1) return -1;   // manda o opcode de jogada (3) e o comando para o fifo de pedidos
  if(write(session.req_pipe, &command, sizeof(char)) != 1) return -1;
  return 0;
}


/// @brief função que disconecta o cliente do servidor
/// @return 0 se sucesso, 1 se erro
int pacman_disconnect() {
  char opcode_disconnect = 2;
  
  if(write(session.req_pipe, &opcode_disconnect, sizeof(char)) != 1) return -1; 
  
  /*------ FECHA OS PIPES DA SESSÃO ------*/
  close(session.req_pipe);
  close(session.notif_pipe);
  debug("pipes closed\n");

  /*----- REMOVE OS PIPES DA SESSÃO -----*/
  unlink(session.req_pipe_path);
  unlink(session.notif_pipe_path);

  /*------ REEINICIAR A SESSÃO -------*/
  session.req_pipe = -1;
  session.notif_pipe = -1;
  strcpy(session.req_pipe_path, "");
  strcpy(session.notif_pipe_path, "");

  return 0;
}


/// @brief recebe atualizações do tabuleiro pelo pipe das notificações
/// @param void 
/// @return Board atualizado
Board receive_board_update(void) {
  char opcode;
  if(read(session.notif_pipe, &opcode, sizeof(char)) != 1) {
    board.game_over = 1;
    return board;
  }

  /*------- LÊ AS INFORMAÇÕES DO TABULEIRO -------*/
  if((read(session.notif_pipe, &board.width, sizeof(int)) != sizeof(int)) ||
  (read(session.notif_pipe, &board.height, sizeof(int)) != sizeof(int)) ||
  (read(session.notif_pipe, &board.tempo, sizeof(int)) != sizeof(int)) ||
  (read(session.notif_pipe, &board.victory, sizeof(int)) != sizeof(int)) ||
  (read(session.notif_pipe, &board.game_over, sizeof(int)) != sizeof(int)) ||
  (read(session.notif_pipe, &board.accumulated_points, sizeof(int)) != sizeof(int))){
    board.game_over = 1;
    return board;
  }

  /*------- LÊ E ALOCA MEMÓRIA PARA A DATA --------*/
  int size = board.width * board.height;
  if(board.data == NULL)
    board.data = malloc(size);
  else 
    board.data = realloc(board.data, size);
  if(read(session.notif_pipe, board.data, size) != size) {
    board.game_over = 1;
    return board;
  }
  
  return board;
}