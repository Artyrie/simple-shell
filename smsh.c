#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>

#define numberchars 1024
#define MAX_LEN 100

/* 커맨드 관리 */
char cmd_line[numberchars];
char* cmd_group[MAX_LEN];

/* 세팅 확인 용 */
int background = 0;
int noclobber = -1;
int ncb_exit = 0;
int pipelined = 0;

/* 히스토리 구현용 간이 딕셔너리 */
char* his[numberchars];
int his_index = 0;
struct History *head;
struct History *tail;

/* 히스토리 구현을 위한 더블 링크드 리스트 */
struct History {
  struct History *next;
  struct History *prev;
  int index;
};

/* 함수 정의 */
void shell_exec(char *cmdline);
int shell_execvp(char *argv[]);
int make_argv(char *tmp_cmd, const char *divider, char** arg_grp);
int check_bg(char *tmp_cmd);
void rollback_rd(int saved_in, int saved_out);
char* check_br(char *tmp_cmd);
void list_add_tail(struct History *tail, int index, char *data);
void list_remove_tail(struct History *tail);
void print_list(struct History *head, struct History *tail);
void free_list(struct History *head);
int ishis_exec(char *tmp_cmd);
void pipeline(char* cmd[]);
int iscd(char cmd[]);
void cd_exec(char cmd[]);

/* standard I/O 저장용 */
int fdin;
int fdout;

/* 메인 함수 */
int main(void) {
  /* std I/O 저장 */
  fdin = dup(STDIN_FILENO);
  fdout = dup(STDOUT_FILENO);

  /* 히스토리 초기화 */
  head = malloc(sizeof(struct History));
  tail = malloc(sizeof(struct History));
  head->index = his_index++;
  head->next = tail;
  head->prev = NULL;
  tail->index = numberchars;
  tail->next = NULL;
  tail->prev = head;
  his[head->index] = NULL;
  his[tail->index] = NULL;

  /* 계속 명령을 받아서 실행 */
  while (1) {
    printf( "%s>", getcwd(cmd_line, sizeof(cmd_line)) );
    fgets(cmd_line, numberchars, stdin);
    /* Enter키 제거 */
    cmd_line[strlen(cmd_line) - 1] = '\0';
    shell_exec(cmd_line);
  }
  return 0;
}

/* 주요 함수들 */

/* cd command 실제 실행 함수 */
void cd_exec(char cmd[]) {
  if (chdir(cmd+3) != 0) {
    perror("No directory");
  }
}

/* cd ---- 형태인지 확인하는 함수 */
int iscd(char cmd[]) {
  if (cmd[0] == 'c' && cmd[1] == 'd' && cmd[2] == ' ') {
    int i;
    for (i=3; i<strlen(cmd); i++) {
      if (cmd[i] == ' ') {
        return 0;
      }
    }
    return 1;
  } else {
    return 0;
  }
}

/* pipe 실행 함수 */
void pipeline(char* cmd[]) {
	int fd[2];
	pid_t pid;
	int fdd = 0;
  char* cmdtmp;
  int i = 0;

	while (cmd[i] != NULL) {
    memcpy(cmdtmp, cmd[i], strlen(cmd[i])+1);
		pipe(fd);
		if ((pid = fork()) == -1) {
			perror("fork");
			exit(1);
		}
		else if (pid == 0) {
			dup2(fdd, 0);
			if (cmd[i+1] != NULL) {
				dup2(fd[1], 1);
			}
			close(fd[0]);
      shell_exec(cmdtmp);
      list_remove_tail(tail);
			exit(1);
		}
		else {
			wait(NULL);
			close(fd[1]);
			fdd = fd[0];
		}
    i++;
	}
}

/* #number 형태인지 검사 */
int ishis_exec(char *tmp_cmd) {
  int i;
  int len = strlen(tmp_cmd);

  if (tmp_cmd[0] != '!') {
    return 0;
  }

  for (i=1; i < len; i++) {
    if (isdigit(tmp_cmd[i]) == 0) {
      return 0;
    }
  }
  return 1;
}

/* history 리스트 해방 */
void free_list(struct History *head) {
  struct History *curr = head->next;
  
  while(curr != NULL) {
    struct History *next = curr->next;
    free(curr);
    curr = next;
  }
  free(head);
}

/* 히스토리 출력 */
void print_list(struct History *head, struct History *tail) {
  struct History *curr = head->next;
  while (curr != tail) {
    printf("%d %s\n", curr->index, his[curr->index]);
    curr = curr->next;
  }
}

/* 히스토리 값 제거 */
void list_remove_tail(struct History *tail) {
  struct History *tmpNode = tail->prev;
  tmpNode->prev->next = tail;
  tail->prev = tmpNode->prev;
  free(tmpNode);
  his_index--;
}

/* 히스토리 값 추가 */
void list_add_tail(struct History *tail, int index, char *data) {
  /* 앞뒤 잇기 */
  struct History *newNode = malloc(sizeof(struct History));
  newNode->prev = tail->prev;
  newNode->next = tail;
  tail->prev->next = newNode;
  tail->prev = newNode;

  /* 데이터 넣기 */
  newNode->index = index;

  /* 간이 딕셔너리 index에 데이터 복사 */
  int len = strlen(data) + 1;
  his[newNode->index] = (char*)malloc(sizeof(char) *len);
  memcpy(his[newNode->index], data, len);
}

/* () 처리 함수 */
char* check_br(char *tmp_cmd) {
  char tmp[numberchars];
  int len = strlen(tmp_cmd);
  int start = -1;
  int end = -1;
  int i;

  memcpy(tmp, tmp_cmd, len+1);
  
  for (i=0; i<len; i++) {
    if (tmp[i] == '(') {
      start = i;
    } else if (tmp[i] == ')') {
      end = i;
    }
  }

  if (start != -1 && end != -1) {
    if (end == strlen(tmp)-1) {
      tmp_cmd[start] = ';';
      tmp_cmd[end] = ';';
      //printf("end-1 : S%sE\n", tmp_cmd);
      return "none";
    } else {
      char *res = malloc(sizeof(char) * numberchars);
      //printf("start : %d, end : %d\n", start, end);
      char* cmd_fbr[MAX_LEN];
      char* cmd_bbr[MAX_LEN];
      char tmp_front[numberchars];
      char tmp_back[numberchars];
      char *sum;
      int i, count, count2;
      int rd = -1;

      memcpy(tmp_front, tmp+start+1, start+end-1);
      memcpy(tmp_back, tmp+end+1, strlen(tmp)-end);
      count = make_argv(tmp_front, ";", cmd_fbr);
      count2 = make_argv(tmp_back, ";", cmd_bbr);

      if (cmd_bbr[0][0] == ' ') {
        memcpy(cmd_bbr[0], cmd_bbr[0]+1, strlen(cmd_bbr[0]));
      }

      for(i=0; i<strlen(cmd_bbr[0]); i++) {
        if (cmd_bbr[0][i] == '>' && cmd_bbr[0][i+1] != '>') {
          rd = i;
        }
      }

      for(i=0; i<count; i++) {
        if (cmd_fbr[i][0] == ' ') {
          memcpy(cmd_fbr[i], cmd_fbr[i]+1, strlen(cmd_fbr[i])+1);
        }
        sum = malloc(sizeof(char) * (strlen(cmd_fbr[i]) + strlen(cmd_bbr[0])));
        strcat(sum, cmd_fbr[i]);
        if (rd != -1 && i != 0) {
          strcat(sum, " >");
        } else {
          strcat(sum, " ");
        }
        strcat(sum, cmd_bbr[0]);
        strcat(sum, ";");
        strcat(res, sum);
      }

      for(i=1; i<count2; i++) {
        strcat(res, cmd_bbr[i]);
        strcat(res, ";");
      }
      return res;
    }
  } else if (start != -1 || end != -1) {
    return "use";
  }
  return "none";
}

/* redirection 풀어주는 함수 */
void rollback_rd(int saved_in, int saved_out) {
  dup2(saved_in, 0);
  close(saved_in);
  dup2(saved_out, 1);
  close(saved_out);
}

/* > < 감지 함수 */
void check_rd(char *tmp_cmd) {
  char *arg;
  int tmplen = strlen(tmp_cmd);
  int fd, i;

  for (i = 0; i<tmplen; i++) {
    /* '>' 수행 */
    if (tmp_cmd[i] == '>' && tmp_cmd[i+1] == '>') {
      arg = strtok(&tmp_cmd[i+2], " \t");
      if ( (fd = open(arg, O_WRONLY | O_APPEND, 0644)) < 0) {
        perror("file open error");
        //ncb_exit = 1;
      }
      dup2(fd, STDOUT_FILENO);
      close(fd);
      tmp_cmd[i] = '\0';
      break;
    /* '>|' 수행 */
    } else if (tmp_cmd[i] == '>' && tmp_cmd[i+1] == '|') {
      arg = strtok(&tmp_cmd[i+2], " \t");
      if ((fd = open(arg, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0) {
          perror("file open error");
          //ncb_exit = 1;
      }
      dup2(fd, STDOUT_FILENO);
      close(fd);
      tmp_cmd[i] = '\0';
      break;
    /* '<' 수행 */ 
    } else if (tmp_cmd[i] == '<') {
      arg = strtok(&tmp_cmd[i+1], " \t");
      if ( (fd = open(arg, O_RDONLY | O_CREAT, 0644)) < 0) {
        perror("file open error");
        //ncb_exit = 1;
      }
      dup2(fd, STDIN_FILENO);
      close(fd);
      tmp_cmd[i] = '\0';
      break;
    /* '>' 수행 */
    } else if (tmp_cmd[i] == '>') {
      arg = strtok(&tmp_cmd[i+1], " \t");
      /* noclobber 설정 X */
      if (noclobber == 0) {
        if ((fd = open(arg, O_WRONLY | O_CREAT | O_EXCL | O_TRUNC, 0644)) < 0) {
          perror("cannot overwrite");
          close(fd);
          ncb_exit = 1;
          break;
        }
      /* noclobber 설정 O */
      } else {
        if ((fd = open(arg, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0) {
          perror("file open error");
          //ncb_exit = 1;
        }
      }
      dup2(fd, STDOUT_FILENO);
      close(fd);
      tmp_cmd[i] = '\0';
      break;
    }
  }
}

/* 백그라운드 여부 확인 함수 */
int check_bg(char *tmp_cmd) {
  int i;

  for (i=0; i < strlen(tmp_cmd); i++) {
    if (tmp_cmd[i] == '&') {
      tmp_cmd[i] = ' ';
      return 1;
    }
  }

  return 0;
}

/* execvp를 사용하기 위해 cmd를 argv에 넣는 함수 */
int make_argv(char *tmp_cmd, const char *divider, char** arg_vector) {
  int num_divided = 0;
  char *tmpstr = NULL;

  /* 뭔가 기호가 없거나 내용이 비었으면 -1 반환 */
  if ( tmp_cmd[0]=='\0' || divider == NULL ) {
    return -1;
  }

  /* strtok으로 받은 문자열을 자름 */
  tmpstr = tmp_cmd;// + strspn(tmp_cmd, divider);
  arg_vector[num_divided] = strtok(tmpstr, divider);
  while (arg_vector[num_divided] != NULL) {
    arg_vector[++num_divided] = strtok(NULL, divider);
  }

  return num_divided;
}

/* execvp 대리 실행 함수
실패시 -1 return, 성공시 0 반환 */
int shell_execvp(char* arg_argv[]) {
  pid_t pid = fork();

  /* fork error 처리 */
  if(pid < 0) {
    fprintf(stderr, "fork failed");
    return -1;
  } else if (pid == 0) {
    execvp(arg_argv[0], arg_argv);
    perror("execvp failed");
    return -1;
    exit(1);
  } else {
    if (background == 0) {
      int status;
      waitpid(pid, &status, 0);
    } else {
    }
    return 0;
  }
}

/* 입력한 cmd 실행 함수 */
void shell_exec(char *cmdline) {
  list_add_tail(tail, his_index++, cmdline);
  char *brtmp;
  brtmp = check_br(cmdline);
  if(strcmp(brtmp, "use") == 0) {
    printf("command group use error\n");
  } else if (strcmp(brtmp, "none") != 0) {
    memcpy(cmdline, brtmp, strlen(brtmp)+1);
  }

  int i=0;
  int count = make_argv(cmdline, ";", cmd_group);
  char cmdtmp[numberchars];
  char* argv[MAX_LEN];
  int count2 = 0;
  int rdtest = -1;
  int rdi = 0;

  for(i=0; i<count; i++) {
    if (cmd_group[i][0] == ' ') {
      /* 맨 앞이 공백문자면 제거
      그냥 복사하면 원본 값이 변경되어 안되므로 memcpy */
      memcpy(cmdtmp, cmd_group[i]+1, strlen(cmd_group[i]) + 1);
    } else {
      memcpy(cmdtmp, cmd_group[i], strlen(cmd_group[i]) + 1);
    }

    for (rdi=0; rdi<strlen(cmdtmp); rdi++) {
      if (cmdtmp[rdi] == '>' && cmdtmp[rdi+1] == '|') {
        rdtest = rdi;
      }
    }

    char* cmd_grp_pipe[MAX_LEN];
    int count_pipe;
    int j;
    if (rdtest != -1) {
      count_pipe = 1;
    } else {
      count_pipe = make_argv(cmdtmp, "|", cmd_grp_pipe);
    }

    for (j=0; j<count_pipe; j++) {
      char cmdtmp2[numberchars];
      if (cmd_grp_pipe[j][0] == ' ') {
        memcpy(cmdtmp2, cmd_grp_pipe[j]+1, strlen(cmd_grp_pipe[j]) + 1);
      } else {
        memcpy(cmdtmp2, cmd_grp_pipe[j], strlen(cmd_grp_pipe[j]) + 1);
      }
    }

    if (count_pipe == 1) {
      /* noclobber 입력 확인 */
      if (strcmp(cmdtmp, "set -o noclobber") == 0) {
        noclobber = 0;
        goto special;
      } else if (strcmp(cmdtmp, "set +o noclobber") == 0) {
        noclobber = 1;
        goto special;
      }
        
      /* history 입력 수행 */
      if (strcmp(cmdtmp, "history") == 0) {
        print_list(head, tail);
        goto special;
      }
        
      /* history #number 의 경우 실행 */
      if (ishis_exec(cmdtmp)) {
        int num = atoi(cmdtmp+1);
        if (num < his_index) {
          list_remove_tail(tail);
          char tmp_cmdline[numberchars];
          memcpy(tmp_cmdline, his[num], strlen(his[num]) + 1);
          shell_exec(tmp_cmdline);
          goto special;
        } else {
        }
      }

      if (iscd(cmdtmp)) {
        cd_exec(cmdtmp);
        goto special;
      }
        
      int saved_in = dup(0);
      int saved_out = dup(1);
      background = check_bg(cmdtmp);
      check_rd(cmdtmp);
      count2 = make_argv(cmdtmp, " \t", argv);

      /* noclobber 설정 시 실행 안되게 하는 용도 */
      if (ncb_exit == 0) {
        shell_execvp(argv);
      }
      ncb_exit = 0;
      /* 수제 명령어 이동 용 */
      special:;
      rollback_rd(saved_in, saved_out);
    } else {
      int saved_in = dup(0);
      int saved_out = dup(1);
      pipeline(cmd_grp_pipe);
      rollback_rd(saved_in, saved_out);
    }
  }
}