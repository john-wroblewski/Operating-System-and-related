/* Program: sqysh
 * Author:  John Wroblewski
 * Desc:    a squishy little shell that supports basic i/o redirection and some basic background process execution
 *
 * Todo:    i would like to replace the realloc() method of building exec_argv by reading args into a linked list
 *          and building the argument array after all are read. this will avoid excessive realloc calls.
 */

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <search.h>

#define CD              1
#define PWD             2
#define EXIT            3
#define MAX_INPUT_SIZE  256
#define NUM_OF_BUILTINS 3
#define MAX_PID         32768
#define DIR_SIZE        1024

void rem_blanks(char *);
int num_dig(int);

int main(int argc, char** argv)
{
   ENTRY e, *ep;
   char ** exec_argv;
   char search_str[5];
   char * token;
   char * command;
   char directory[DIR_SIZE];
   char * cp;
   char * redir_in = NULL;
   char * redir_out = NULL;
   char buf[MAX_INPUT_SIZE];
   const char * prompt = "sqysh$ ";
   const char * built_ins[] = { "cd", "pwd", "exit" };
   int status;
   int fd_in;
   int fd_out;
   int background = 0;
   int batch_mode = 0;
   int built_in =   0;
   int c, i;
   int exec_argc;
   FILE * instream = stdin;
   pid_t pid;

   if(!hcreate(MAX_PID))
   {
      fprintf(stderr, "hash table allocation failed\n");
      return 1;
   }

   if(argc == 1)
   {
      if(!isatty(0))
         batch_mode = 1;
   }
   else if(argc == 2)
   {
      instream = fopen(argv[1], "r");
      batch_mode = 1;
      if(instream == NULL)
      {
         fprintf(stderr, "%s: %s\n", argv[1], strerror(errno));
         abort();
      }
   }
   else
   {
      fprintf(stderr, "%s: too many arguments.\n", argv[0]);
      abort();
   }

   if(!batch_mode)
      printf("%s", prompt);
   if(!fgets(buf, MAX_INPUT_SIZE, instream))
      return 0;

   do
   {
      //checking for finished children [post prompt print]
      while((c = waitpid(-1, &status, WNOHANG)) > 0)
      {
         sprintf(search_str, "%d", c);
         e.key = search_str;
         ep = hsearch(e, FIND);
         fprintf(stderr, "[%s (%d) completed with status %d]\n", (char *)ep->data, c, WEXITSTATUS(status));
         free(ep->key);
         free(ep->data);
      }

      built_in = 0;
      rem_blanks(buf);
      redir_in = redir_out = NULL;
      background = 0;
      command = strtok(buf, " \n");
      exec_argc = 1;

      if(!command)
         goto NULL_COMMAND; //goto for the win!!

      if(!(exec_argv = malloc(sizeof(char *))))
      {
         fprintf(stderr, "allocation error\n");
         return 1;
      }

      //determine if we're dealing with a builtin
      for(i = 0;i < NUM_OF_BUILTINS;i++)
      {
         if(!strcmp(command, built_ins[i]))
         {
            built_in = i + 1;
            break;
         }
      }

      //populate the argv array for the execvp (or cd) call
      //if we see special syntax chars, handle them here
      exec_argv[0] = command;
      while((token = strtok(NULL, " \n")))
      {
         if(!strcmp(token, "<"))
            redir_in = strtok(NULL, " \n");
         else if(!strcmp(token, ">"))
            redir_out = strtok(NULL, " \n");
         else if(!strcmp(token, "&"))
            background = 1;
         else
         {
            exec_argc++;
            if(!(exec_argv = realloc((void *)exec_argv, exec_argc * sizeof(char *))))
            {
               fprintf(stderr, "allocation error!\n");
               abort();
            }
            exec_argv[exec_argc - 1] = token;
         }
      }
      //terminate the argument array with a null pointer
      if(!(exec_argv = realloc((void *)exec_argv, (exec_argc + 1) * sizeof(char *))))
      {
         fprintf(stderr, "allocation error!\n");
         abort();
      }
      exec_argv[exec_argc] = 0;

      switch(built_in){
         case CD:
            if(exec_argc > 2)
               fprintf(stderr, "cd: too many arguments\n");
            else if(exec_argc == 1)
            {
               if(chdir(getenv("HOME")))
                  fprintf(stderr, "cd: %s: %s\n", getenv("HOME"), strerror(errno));
            }
            else if(chdir(exec_argv[1]))
               fprintf(stderr, "cd: %s: %s\n", exec_argv[1], strerror(errno));
            break;
         case EXIT:
            free(exec_argv);
            exit(0);
         case PWD:
            if(exec_argc > 1)
            {
               fprintf(stderr, "error: pwd takes no arguments\n");
               break;
            }
            else if(!(getcwd(directory, DIR_SIZE)))
            {
               fprintf(stderr, "%s: %s\n", "getcwd", strerror(errno));
               break;
            }
            printf("%s\n", directory);
            break;
         default:
            break;
      }

      if(!built_in && *command)
      {
         if((pid = fork()) == 0)
         {//in the child. here we use execvp

            if(redir_in)
            {
               fd_in = open(redir_in, O_RDONLY);
               dup2(fd_in, 0);
            }
            if(redir_out)
            {
               fd_out = open(redir_out, O_WRONLY | O_CREAT | O_TRUNC, 0644);
               dup2(fd_out, 1);
            }

            if(execvp(command, exec_argv) == -1)
            {
               fprintf(stderr, "%s: %s\n", command, strerror(errno));
               free(exec_argv);
               exit(1);
            }
         }
         else if(pid > 0)
         {//in the parent. manage waiting/pid hash table here
            if(!background)
               waitpid(pid, NULL, 0);
            else
            {
               if(!(cp = malloc((num_dig(pid) + 1) * sizeof(char))))
               {
                  fprintf(stderr, "allocation error!\n");
                  abort();
               }
               sprintf(cp, "%d", pid);
               e.key = cp;

               if(!(cp = malloc((strlen(command) + 1) * sizeof(char))))
               {
                  fprintf(stderr, "allocation error!\n");
                  abort();
               }
               strcpy(cp, command);
               e.data = cp;

               ep = hsearch(e, ENTER);
               if(ep == NULL)
               {
                  fprintf(stderr, "entry failed\n");
                  exit(EXIT_FAILURE);
               }
            }
         }
      }

      free(exec_argv);

NULL_COMMAND:

      //checking for finished children [pre prompt print]
      while((c = waitpid(-1, &status, WNOHANG)) > 0)
      {
         sprintf(search_str, "%d", c);
         e.key = search_str;
         ep = hsearch(e, FIND);
         fprintf(stderr, "[%s (%d) completed with status %d]\n", (char *)ep->data, c, WEXITSTATUS(status));
         free(ep->key);
         free(ep->data);
      }

      if(!batch_mode)
         printf("%s", prompt);
   }
   while(fgets(buf, MAX_INPUT_SIZE, instream));

   hdestroy();

   return 0;
}

void rem_blanks(char *string) {
        if(*string != ' ' && *string != '\t')
                return;
        int i, k = 0;
        for(i = 0; string[ i ] == ' ' || string[ i ] == '\t'; i++)
                {       }
        while((string[k++] = string[i++]))
                {       }
        return;
}


//assumption: input will be at most 5 digits (reasonable as our pid's max out at 2^15
int num_dig(int n) {
   if(n < 10) return 1;
   if(n < 100) return 2;
   if(n < 1000) return 3;
   if(n < 10000) return 4;
   return 5;
}
