#include<stdio.h>
#include<unistd.h>
#include<string.h>
#include<stdlib.h>
#include<sys/wait.h>
#include<fcntl.h>
#include<sys/stat.h>
#define MAX_INPUT_LENGTH 512+1
#define MAX_BG_JOBS 128
int bg_jobs[MAX_BG_JOBS];
int num_bg_jobs=0;
void myerror(){
    char error_message[30] = "An error has occurred\n";
    write(STDERR_FILENO, error_message, strlen(error_message));
}

void mycd(char *path){
    if(path==NULL){
        char *home_dir=getenv("HOME");
        chdir(home_dir);
    }else{
        if(chdir(path)!=0){
            myerror();
        }
    }
}
void mypwd(){
    char cur_dir[MAX_INPUT_LENGTH];
    getcwd(cur_dir,sizeof(cur_dir));
    int len=strlen(cur_dir);
    cur_dir[len]='\n',cur_dir[len+1]='\0';
    write(STDOUT_FILENO, cur_dir, strlen(cur_dir));
}
void myredirect(int argc,char *args[]){
    int redirect=0;
    char *outfile=NULL;
    for(int i=0;i<argc;i++){
        if(strcmp(args[i],">")==0){
            redirect=1;
            outfile=args[i+1];
            if(i+2<argc){
                myerror();
            }
            args[i]=NULL;
            break;
        }
    }
    if(redirect){
        int fd=open(outfile,O_WRONLY|O_CREAT|O_TRUNC,777);
        if(fd==-1){
            myerror();
            exit(1);
        }
        if (chmod(outfile, 0777) == -1) {
            myerror();
            exit(1);
        }
        close(STDOUT_FILENO);
        //Copy file descriptor fd to standard output file descriptor STDOUT_FILENO
        dup2(fd,STDOUT_FILENO);
        close(fd);
    }
}
void mywait(){
    for(int i=0;i<num_bg_jobs;i++){
        int status;
        waitpid(bg_jobs[i],&status,0);
    }
    num_bg_jobs=0;
}
void exec_cmd(char *cmd){
    char *args[MAX_INPUT_LENGTH];
    //tokenize by whitespace and newline
    char *token=strtok(cmd," \t\n");
    int i=0;
    while(token!=NULL){
        char *rd=strchr(token,'>');
        if(rd!=NULL){
            char *ttoken=token;
            token=rd+1;
            (*rd)='\0';
            if((*ttoken)!='\0'){
                args[i++]=ttoken;
            }
            args[i++]=">";
        }
        if((*token)!='\0'){
            args[i++]=token;
        }
        token=strtok(NULL," \t\n");
    }
    if(i==0){
        return;
    }
    //the list of arguments must be terminated with a NULL pointer
    args[i]=NULL;
    myredirect(i,args);
    execvp(args[0],args);//Will not return
    myerror();
    exit(1);
}
void exec_pipe(char *input){
    char tmp[MAX_INPUT_LENGTH];
    strcpy(tmp,input);
    char *built_in_cmd=strtok(tmp," \t\n");
    char *next=strtok(NULL," \t\n");
    if(built_in_cmd!=NULL){
        if(strcmp(built_in_cmd,"exit")==0){
            if(next){
                myerror();
                return;
            }else{
                exit(0);
            }
        }else if(strcmp(built_in_cmd,"cd")==0){
            mycd(next);
            return;
        }else if(strcmp(built_in_cmd,"pwd")==0){
            if(next){
                myerror();
            }else{
                mypwd();
            }
            return;
        }else if(strcmp(built_in_cmd,"wait")==0){
            if(next){
                myerror();
            }else{
                mywait();
            }
            return;
        }
    }

    //check if the command end with '&'
    int bg=0;
    if(strlen(input)>1&&input[strlen(input)-2]=='&'){
        bg=1;
        input[strlen(input)-2]='\0';
    }
    char *cmds[MAX_INPUT_LENGTH];
    char *token=strtok(input,"|");
    int cmd_nums=0;
    while(token!=NULL){
        cmds[cmd_nums++]=token;
        token=strtok(NULL,"|");
    }

    int pipefds[2];
    int prev_pipe_read = -1;

    for (int i = 0; i < cmd_nums; i++) {
        pipe(pipefds);

        int pid = fork();
        if (pid == 0) {
            // Child process
            if (i > 0) {
                // Redirect stdin from the previous pipe
                close(STDIN_FILENO);
                dup2(prev_pipe_read, STDIN_FILENO);
                close(prev_pipe_read);
            }

            if (i < cmd_nums - 1) {
                // Redirect stdout to the current pipe
                close(STDOUT_FILENO);
                dup2(pipefds[1], STDOUT_FILENO);
                close(pipefds[1]);
            }

            // Execute the command
            exec_cmd(cmds[i]);

            exit(1);
        } else if (pid > 0) {
            // Parent process
            if (prev_pipe_read != -1) {
                close(prev_pipe_read);
            }

            // Save the read end of the current pipe for the next iteration
            prev_pipe_read = pipefds[0];

            // Close the write end of the pipe
            close(pipefds[1]);

            // Wait for the child to complete
            if(!bg){
                //wait for the child to complete if not a background job
                waitpid(pid,NULL,0);
            }else{
                if(num_bg_jobs<MAX_BG_JOBS){
                    bg_jobs[num_bg_jobs++]=pid;//add the background job to the list
                }else{
                    myerror();
                }
            }
        }
    }

    // Close the last remaining pipe read end
    if (prev_pipe_read != -1) {
        close(prev_pipe_read);
    }
}
int main(int argc, char *argv[]){
    char input[MAX_INPUT_LENGTH*2];
    if(argc==2){//batch mode: read from a batch file
        FILE *batch_file=fopen(argv[1],"r");
        if(batch_file==NULL){
            myerror();
            exit(1);
        }
        while(fgets(input,sizeof(input),batch_file)!=NULL){
            if(strlen(input)>MAX_INPUT_LENGTH){
                myerror();
                continue;
            }
            write(STDOUT_FILENO,input,strlen(input));
            exec_pipe(input);
        }
        fclose(batch_file);
    }else if(argc==1){//interactive mode
        while(1){
            printf("mysh> ");
            fflush(stdout);
            if(fgets(input,sizeof(input),stdin)==NULL){//ctrl+D
                //wait for any remaining background jobs before exiting
                mywait();
                break;
            }
            if(strlen(input)>MAX_INPUT_LENGTH){
                myerror();
                continue;
            }
            exec_pipe(input);
        }
    }else{
        myerror();
        exit(1);
    }
    return 0;
}