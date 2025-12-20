#include "board.h"
#include "parser.h"
// #include "game.h"
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <pthread.h>

typedef struct SessionArguments {
   int client_id;
   int req_pipe;
   int notif_pipe;
   board_t board;
   char* levels_dir;
   files_t session_files;
   int current_level;
   int accumulated_points;
}SessionArguments;

typedef struct {
    struct SessionArguments *sessionArguments;
    int ghost_index;
} ghost_thread_arg_t;

typedef struct {
    board_t *board;
    int *req_pipe;
} pacman_thread_arg_t;

int victory = 0;
int game_over = 0;

void* pacman_thread(void *arg) {
   struct SessionArguments *args = arg;
   board_t *board = &args->board;
   int req_pipe = args->req_pipe;
   pacman_t* pacman = &board->pacmans[0];
   
   //int *retval = malloc(sizeof(int));
   while (1) {
      sleep_ms(board->tempo * (1 + pacman->passo));
      debug("---THREAD DO PACMAN---\n");
      command_t* play;
      command_t c;
      if (pacman->n_moves == 0) {
         debug("O pacman manual começou\n");
         char opcode;
         char command_client;
         if(read(req_pipe, &opcode, sizeof(char)) <= 0){
            game_over = 1;
            break;
         }
         if(opcode != 3) {
            continue;
         }
         read(req_pipe, &command_client, sizeof(char));
         c.command = command_client;
         c.turns = 1;
         play = &c;
      }
      else {
         play = &pacman->moves[pacman->current_move%pacman->n_moves];
      }

      debug("Comando: %c\n", play->command);

      // QUIT
      if (play->command == 'Q') {
         game_over = 1;
         //*retval = QUIT_GAME;
         //return (void*) retval;
      }
      /*// FORK
      if (play->command == 'G') {
         *retval = CREATE_BACKUP;
         return (void*) retval;
      }*/

      pthread_rwlock_rdlock(&board->state_lock);

      int result = move_pacman(board, 0, play);
      debug("O pacman moveu-se com o move_pacman\n");
      debug("result do move pacman --> %d\n", result);
      if (result == REACHED_PORTAL) {
         // Next level
         debug("NEX_LEVEL\n");
         victory = 1;
         //*retval = NEXT_LEVEL;
         break;
      }

      if(result == DEAD_PACMAN) {
            // Restart from child, wait for child, then quit
         debug("DEAD_PACMAN -> QUIT_GAME\n");
         game_over = 1;
         debug("CHEGA AO DEAD_PACMAN QUIT_GAME\n");
         //*retval = LOAD_BACKUP;
         break;
      }

      pthread_rwlock_unlock(&board->state_lock);
   }
   //pthread_rwlock_unlock(&board->state_lock);
   //return (void*) retval;
   return NULL;
}

void* ghost_thread(void *arg) {
   ghost_thread_arg_t *arg_ghost = arg;
   struct SessionArguments *args = arg_ghost->sessionArguments;
   board_t *board = &args->board;
   int ghost_ind = arg_ghost->ghost_index;

   free(arg_ghost);

   ghost_t* ghost = &board->ghosts[ghost_ind];

   while (1) {
      sleep_ms(200);
      sleep_ms(board->tempo * (1 + ghost->passo));
      debug("---THREAD DO GHOST---\n");

      pthread_rwlock_rdlock(&board->state_lock);
         if(game_over) {
         debug("  game_over detetado\n");

         pthread_rwlock_unlock(&board->state_lock);
         pthread_exit(NULL);
      }
        
      move_ghost(board, ghost_ind, &ghost->moves[ghost->current_move%ghost->n_moves]);
      debug("O ghost moveu-se com o move_ghost\n");

      pthread_rwlock_unlock(&board->state_lock);
   }
}

void* session_thread (void* arg) {
   debug("\nsession_thread:\n");
  
   /* ------- ARGUMENTOS DA THREAD ------- */
   struct SessionArguments *args = (struct SessionArguments*) arg;
   int notif_pipe = args->notif_pipe;
   //int req_pipe = args->req_pipe;
   char* level_dir = args->levels_dir;
   int current_level = args->current_level;
   int accumulated_points = args->accumulated_points;
   //board_t *board = &args->board;

   /*------------LER DIRETORIA-------------*/
   args->session_files = manage_files(level_dir);
   debug("    Lida a diretoria %s e colocada numa estrutura files_t\n", level_dir);
   files_t files = args->session_files;
   current_level = 0;
   accumulated_points = 0;
   
   /*------------LOAD LEVEL-------------*/
   load_level(&args->board, files.level_files[current_level], level_dir, accumulated_points);
   victory = 0;
   game_over = 0;
   debug("    nível %s carregado\n", files.level_files[current_level]);

   pthread_t pacman_tid;
   pthread_t *ghost_tids = malloc(args->board.n_ghosts * sizeof(pthread_t));
   pthread_create(&pacman_tid, NULL, pacman_thread, (void*) args);
   for (int i = 0; i < args->board.n_ghosts; i++) {
      ghost_thread_arg_t *arg_ghost = malloc(sizeof(ghost_thread_arg_t));
      arg_ghost->sessionArguments = args;
      arg_ghost->ghost_index = i;
      pthread_create(&ghost_tids[i], NULL, ghost_thread, (void*) arg_ghost);
   }
   debug("    Pacman e ghost thread criadas\n");
   
   /*----------LÊ E ENVIA TABULEIRO-----------*/
   board_t temp;
   while(1){
         debug("=== INÍCIO DO LOOP DE ENVIO DO BOARD ===\n");

      if(victory){
         debug("entrou no victory\n");
         accumulated_points = args->board.pacmans[0].points;
         current_level++;
         unload_level(&args->board);

         if(current_level != files.level_count){
            load_level(&args->board, files.level_files[current_level], level_dir, accumulated_points);
            victory = 0;

            debug("    nível %s carregado\n", files.level_files[current_level]);
            pthread_t pacman_tid;
            pthread_t *ghost_tids = malloc(args->board.n_ghosts * sizeof(pthread_t));
            pthread_create(&pacman_tid, NULL, pacman_thread, (void*) args);
            for (int i = 0; i < args->board.n_ghosts; i++) {
               ghost_thread_arg_t *arg_ghost = malloc(sizeof(ghost_thread_arg_t));
               arg_ghost->sessionArguments = args;
               arg_ghost->ghost_index = i;
               pthread_create(&ghost_tids[i], NULL, ghost_thread, (void*) arg_ghost);
            }
            debug("    Pacman e ghost thread criadas\n");
         }
      }
            
      pthread_rwlock_rdlock(&args->board.state_lock);
      temp = args->board;   // cópia da struct
      pthread_rwlock_unlock(&args->board.state_lock);
      
      char opcode = 4;

      // se o board ainda não foi inicializado no load_level, não escrever nada (porque, se escrevesse, iria ser lixo)
      if (temp.width == 0 && temp.height == 0 && temp.board == NULL) {
         debug("  Board ainda não está pronto, aguardando...\n");
         sleep_ms(100000);
         continue; 
      }

      write(notif_pipe, &opcode, sizeof(char));
      write(notif_pipe, &temp.width, sizeof(int));
      write(notif_pipe, &temp.height, sizeof(int));
      write(notif_pipe, &temp.tempo, sizeof(int));
      write(notif_pipe, &victory, sizeof(int));                            // victory
      debug("O server esta a escrever no victory: %d\n", victory);
      write(notif_pipe, &game_over, sizeof(int));                          // game_over
      debug("O server esta a escrever no game_over: %d\n", game_over);
      write(notif_pipe, &temp.pacmans[0].points, sizeof(int));             // accumulated_points
      char *data = malloc(temp.width * temp.height);

      // TO-DO nao percebeo porque nao esta a dar no server-debug.log
      debug("  As infos do tabuleiro foram escritas no pipe\n");
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
      
      sleep_ms(200); 
      // TO-DO VER DATA
      debug("=== FIM DA ITERAÇÃO, VOLTANDO AO LOOP ===\n");

   /*------------UNLOAD LEVEL, level++ -------------*/

   }
   close(notif_pipe);
   close(args->req_pipe);
   return NULL;
}



int main(int argc, char *argv[]) { // PacmanIST levels_dir max_games nome_do_FIFO_de_registo
   if(argc != 4) return -1;
   char *levels_dir = argv[1];
   //int max_games = atoi(argv[2]);
   const char *server_pipe_path = argv[3];

   int server_fd, notif_fd, request_fd/*, result_main*/;
   unlink(server_pipe_path);
   open_debug_file("server-debug.log");
   debug("Servidor iniciado\n");


   /*-------- CRIA O FIFO DO SERVIDOR ----------*/
   if(mkfifo(server_pipe_path, 0666) < 0) return -1;
   server_fd = open(server_pipe_path, O_RDONLY);
   if(server_fd < 0) return -1;

   char opcode, req_pipe_path[40], notif_pipe_path[40], opcode_result = 1, result = 0;
   read(server_fd, &opcode, sizeof(char));               // lê opcode enviado pelo client

   /*------------CASO DO CONNECT-----------*/
   if(opcode == 1){                                      // se for pedido de inicio de sessao
      
      debug("Cliente pediu para se conectar\n");
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
      args->levels_dir = levels_dir;

      pthread_t tid_session;
      pthread_create(&tid_session, NULL, session_thread, args);
      debug("Começo do jogo com a criaçõa da session_thread");
      
      //result_main = start_session(levels_dir);     // TRATAAAAR
      //printf("%d\n", result_main);
      pthread_join(tid_session, NULL);
      free(args);

   }
   close_debug_file();

   return 0;
}

