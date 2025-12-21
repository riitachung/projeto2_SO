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

/*--------- ESTRUTURAS -----------*/
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

/*---------VARIÁVEIS GLOBAIS -----------*/

int victory = 0;
int game_over = 0;

pthread_rwlock_t victory_lock;  

/*--------- TAREFA DO PACMAN -----------*/
void* pacman_thread(void *arg) {
   struct SessionArguments *args = arg;
   board_t *board = &args->board;
   int req_pipe = args->req_pipe;
   pacman_t* pacman = &board->pacmans[0];
   
   while (1) {
      sleep_ms(board->tempo * (1 + pacman->passo));
      debug("---THREAD DO PACMAN---\n");

      command_t* play;
      command_t c;
      // PACMAN MANUAL
      if (pacman->n_moves == 0) {
         debug("O pacman manual começou\n");
         char opcode;
         char command_client;
         if(read(req_pipe, &opcode, sizeof(char)) <= 0){             // Lê opcode de pedido de comando
            pthread_rwlock_wrlock(&victory_lock);
            game_over = 1;
            pthread_rwlock_unlock(&victory_lock);
            break;
         }
         if(opcode == 2) {                                           // pedido para disconnect
            pthread_rwlock_wrlock(&victory_lock);
            game_over = 1;
            pthread_rwlock_unlock(&victory_lock);
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
      // PACMAN AUTOMÁTICO
      else {
         play = &pacman->moves[pacman->current_move%pacman->n_moves];
      }

      debug("Comando: %c\n", play->command);

      // QUIT
      if (play->command == 'Q') {
         pthread_rwlock_wrlock(&victory_lock);
         game_over = 1;
         pthread_rwlock_unlock(&victory_lock);
      }

      pthread_rwlock_wrlock(&board->state_lock);

      int result = move_pacman(board, 0, play);
      debug("result do move pacman --> %d\n", result);
       // NEXT LEVEL
      if (result == REACHED_PORTAL) {
         debug("NEXT_LEVEL\n");
         pthread_rwlock_wrlock(&victory_lock);
         victory = 1;
         pthread_rwlock_unlock(&victory_lock);
         pthread_rwlock_unlock(&board->state_lock);

         break;
      }
      // PACMAN MORTO
      if(result == DEAD_PACMAN) {
         debug("DEAD_PACMAN -> QUIT_GAME\n");
         pthread_rwlock_wrlock(&victory_lock);
         game_over = 1;
         pthread_rwlock_unlock(&victory_lock);
         debug("CHEGA AO DEAD_PACMAN QUIT_GAME\n");
         pthread_rwlock_unlock(&board->state_lock);

         //*retval = LOAD_BACKUP;
         break;
      }

      pthread_rwlock_unlock(&board->state_lock);
   }
   debug("vou acabar pacman thread\n");
   return NULL;
}

/*---------- THREAD DO MONSTRO -----------*/
void* ghost_thread(void *arg) {
   ghost_thread_arg_t *arg_ghost = arg;
   struct SessionArguments *args = arg_ghost->sessionArguments;
   board_t *board = &args->board;
   int ghost_ind = arg_ghost->ghost_index;

   free(arg_ghost);
   ghost_t* ghost = &board->ghosts[ghost_ind];

   while (1) {
      sleep_ms(board->tempo);

      pthread_rwlock_rdlock(&victory_lock);
      int current_game_over = game_over;
      int current_victory = victory;
      pthread_rwlock_unlock(&victory_lock);
      
      if(current_game_over || current_victory) {
         debug("game_over detetado\n");
         debug("vou acabar ghost thread\n");
         pthread_exit(NULL);
      }

      pthread_rwlock_rdlock(&board->state_lock);
      move_ghost(board, ghost_ind, &ghost->moves[ghost->current_move%ghost->n_moves]);
      pthread_rwlock_unlock(&board->state_lock);
   }
}


/*---------- THREAD GESTORA DE SESSÃO -----------*/
void* session_thread (void* arg) {
   // ARGUMENTOS DA THREAD
   struct SessionArguments *args = (struct SessionArguments*) arg;
   int notif_pipe = args->notif_pipe;
   char* level_dir = args->levels_dir;
   int current_level = args->current_level;
   int accumulated_points = args->accumulated_points;
   int threads_running = 1;

   // LÊ DIRETORIA
   args->session_files = manage_files(level_dir);
   debug("Lida a diretoria %s e colocada numa estrutura files_t\n", level_dir);
   files_t files = args->session_files;
   current_level = 0;
   accumulated_points = 0;
   
   // DÁ LOAD AO PRIMEIRO NÍVEL
   load_level(&args->board, files.level_files[current_level], level_dir, accumulated_points);
   debug("nível %s carregado\n", files.level_files[current_level]);

   // COMEÇA AS THREADS
   int n_ghosts = args->board.n_ghosts;
   pthread_t pacman_tid;
   pthread_t *ghost_tids = malloc(n_ghosts * sizeof(pthread_t));
   pthread_create(&pacman_tid, NULL, pacman_thread, (void*) args);
   for (int i = 0; i < n_ghosts; i++) {
      ghost_thread_arg_t *arg_ghost = malloc(sizeof(ghost_thread_arg_t));
      arg_ghost->sessionArguments = args;
      arg_ghost->ghost_index = i;
      pthread_create(&ghost_tids[i], NULL, ghost_thread, (void*) arg_ghost);
   }
   debug("Pacman e ghost threads criadas\n");
   
   // LÊ E ENVIA PERIODICAMENTE O TABULEIRO
   board_t temp;
   while(1){
      
      pthread_rwlock_rdlock(&args->board.state_lock);
      temp = args->board;   // cópia da struct
      pthread_rwlock_unlock(&args->board.state_lock);
      
      pthread_rwlock_rdlock(&victory_lock);
      int current_victory = victory;
      int current_game_over = game_over;
      pthread_rwlock_unlock(&victory_lock);
      
      char opcode = 4;

      // se o board ainda não foi inicializado no load_level, não escrever nada
      if (temp.width == 0 && temp.height == 0 && temp.board == NULL) {
         debug("Board ainda nao disponível\n");
         //sleep_ms(1000);
         continue; 
      }

      // ESCREVE NO PIPE DAS NOTIFICAÇÕES A INFORMAÇÃO PARA CONSTRUIR TABULEIRO
      write(notif_pipe, &opcode, sizeof(char));
      write(notif_pipe, &temp.width, sizeof(int));
      write(notif_pipe, &temp.height, sizeof(int));
      write(notif_pipe, &temp.tempo, sizeof(int));
      write(notif_pipe, &current_victory, sizeof(int));                            
      write(notif_pipe, &current_game_over, sizeof(int));                          
      write(notif_pipe, &temp.pacmans[0].points, sizeof(int));  
      
      char *data = malloc(temp.width * temp.height);

      for (int i = 0; i < temp.width * temp.height; i++) {
         if(temp.board[i].content == 'M'){
            data[i] = 'M';
         } else if (temp.board[i].content == 'W') {
            data[i] = '#';
         } else {
            if(temp.board[i].has_dot == 1)
               data[i] = '.';
            else if (temp.board[i].has_portal == 1)
               data[i] = '@';
            else data[i] = ' ';
         }
      }
      int x = temp.pacmans[0].pos_x;
      int y = temp.pacmans[0].pos_y;
      int idx = y * temp.width + x;
      if (idx >= 0 && idx < temp.width * temp.height) {
         data[idx] = 'C';
      }

      write(notif_pipe, data, (temp.width * temp.height));
      free(data);
      
      // VERIFICA SE DEVE SAIR (APÓS ENVIAR O BOARD)
      if (/*(current_victory == 1 && */current_game_over == 1) {
         debug("O jogo terminou completamente\n");
         break;
      }  

      // no caso de vitória (reached_portal)
      if(current_victory) {
         accumulated_points = args->board.pacmans[0].points;  // Guardar pontos
         current_level++;
         pthread_join(pacman_tid, NULL);
         for(int i = 0; i < n_ghosts; i++){
            pthread_join(ghost_tids[i], NULL);
         }
         free(ghost_tids);
         threads_running = 0;
         // se ainda houver mais levels para jogar
         if(current_level < files.level_count){
            // FAZER UNLOAD ANTES DE CARREGAR O PRÓXIMO
            unload_level(&args->board);
            load_level(&args->board, files.level_files[current_level], level_dir, accumulated_points);
            debug("nível %s carregado\n", files.level_files[current_level]);
            
            // REINICIA VICTORY E GAME OVER = 0 DEPOIS DE DAR LOAD A UM NOVO LEVEL
            pthread_rwlock_wrlock(&victory_lock);
            victory = 0;
            game_over = 0;
            pthread_rwlock_unlock(&victory_lock);

            // recomeça as threads
            //pthread_t pacman_tid;
            n_ghosts = args->board.n_ghosts;
            ghost_tids = malloc(n_ghosts * sizeof(pthread_t));
            pthread_create(&pacman_tid, NULL, pacman_thread, (void*) args);
            for (int i = 0; i < n_ghosts; i++) {
               ghost_thread_arg_t *arg_ghost = malloc(sizeof(ghost_thread_arg_t));
               arg_ghost->sessionArguments = args;
               arg_ghost->ghost_index = i;
               pthread_create(&ghost_tids[i], NULL, ghost_thread, (void*) arg_ghost);
            }
            debug("Pacman e ghost thread criadas\n");

         } else {
            // último nível completo
            pthread_rwlock_wrlock(&victory_lock);
            game_over = 1;
            pthread_rwlock_unlock(&victory_lock);
            debug("Último nível completo, game_over setado\n");
            // NÃO FAZER UNLOAD - ainda precisa do board para enviar o último frame
         }
         debug("vou voltar ao loop\n");
      }
      
      sleep_ms(temp.tempo); 
      debug("=== FIM DA ITERAÇÃO, VOLTANDO AO LOOP ===\n");
   }
   
   // ESPERA PELAS THREADS
   if(threads_running){
      pthread_join(pacman_tid, NULL);
      for(int i = 0; i < n_ghosts; i++) {
         pthread_join(ghost_tids[i], NULL);
      }
      debug("esperei pelas threads\n");
      free(ghost_tids);
   }
   
   // AGORA SIM, FAZER UNLOAD DO ÚLTIMO NÍVEL
   unload_level(&args->board);

   debug("unload do último nível feito\n");
   close(args->req_pipe);
   close(args->notif_pipe);
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
   while(1){
      read(server_fd, &opcode, sizeof(char));               // lê opcode enviado pelo client

   /*------------CASO DO CONNECT-----------*/
  
      if(opcode == 1){                                      // se for pedido de inicio de sessao
         
         debug("Cliente pediu para se conectar\n");
         if(read(server_fd, req_pipe_path, sizeof(req_pipe_path)) != 40) continue;
         if(read(server_fd, notif_pipe_path, sizeof(notif_pipe_path)) != 40) continue;

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
   
         pthread_join(tid_session, NULL);
         free(args);
      }
   }
   
   pthread_rwlock_destroy(&victory_lock);
   close_debug_file();

   return 0;
}

