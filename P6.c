#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#define numChildren 5
#define READ_PIPE 0
#define WRITE_PIPE 1
#define TIME_TO_RUN 30
// MAX_SLEEP_TIME is in seconds
#define MAX_SLEEP_TIME 2
// TV_WAIT_TIME is in seconds
#define TV_WAIT_TIME 2

#define TIME_BUFF_SIZE 10
#define BUFF_SIZE 1024

struct timeval startTV;

/* Sets up pipes for processes
 * @retval an array with numChild entries, which are arrays of size 2 of file descriptors
 *      the pipes are heap allocated and opened
 *      if a pipe cannot be created, the program is terminated
 */
int **getPipes() {
    int i, **pipes = (int **) malloc(numChildren*sizeof(int *));
    for(i = 0; i < numChildren; i++) {
        pipes[i] = (int *) malloc(2*sizeof(int));
        if(pipe(pipes[i]) == -1) {
            fprintf(stderr, "pipe(2) failed with errno %d\n", errno);
            exit(errno);
        }
    }
    return pipes;
}

/* frees the memory for the pipes created by getPipes
 * it assumes the pipes are already closed when they are passed to this function
 */
void freePipes(int **pipes) {
    int i;
    for(i = 0; i < numChildren; i++) {
        free(pipes[i]);
    }
    free(pipes);
}

/* @retval a heap allocated char * of the form "X:YY.ZZZ:" where 
 *      X is the number of minutes
 *      YY is the number of seconds
 *      ZZZ is the number of milliseconds
 */
char *getTime() {
    struct timeval now;
    char *retval = (char *)calloc(TIME_BUFF_SIZE+1, sizeof(char));
    gettimeofday(&now, NULL);
    int msSinceStart = (now.tv_usec-startTV.tv_usec)/1000;
    if(msSinceStart < 0) {
        msSinceStart = ((now.tv_usec+1000000)-startTV.tv_usec)/1000;
    }
    int secsSinceStart = now.tv_sec-startTV.tv_sec;
    snprintf(retval, TIME_BUFF_SIZE, "0:%02d.%03d:", secsSinceStart, msSinceStart);
    return retval;
}

/* Write buff to the stream and filter out invalid characters
 * I found this to be necessary as sometimes read would put an invalid ascii 
 * character at the end of the buffer before the '\0'
 * @param timeBuff
 *      the current time to display before printing buff
 *      this will most likely be generated by getTime()
 * @param buff
 *      the data to print to the stream, this should be a null terminated string
 * @param readVal
 *      the number of bytes read by read(3)
 * @param stream
 *      the stream to print the information to
 */
void writeCarefully(char *timeBuff, char *buff, int readVal, FILE *stream) {
    int i;
    fwrite(timeBuff, sizeof(char), TIME_BUFF_SIZE-1, stream);
    for(i = 0; i < readVal; i++) {
        if(buff[i] == 9 || buff[i] == 10 || buff[i] == 13 || (buff[i] >= 32 && buff[i] <= 126)) {
            fputc(buff[i], stream);
        }
        if(i == readVal-1 && buff[i] != 10 && buff[i] != 13) {
            if(buff[i] == 0 && buff[i-1] != 10 && buff[i-1] != 13) {
                printf("buff[i] == %d\n", buff[i]);
                fputc('\n', stream);
            }
        }
    }
}

/* The function for the main program
 * It reads from all of the pipes and prints the information to a file and stdout
 * it uses the select function to inform it when there is information to read
 * @param pipes
 *      an array with numChild entries, which are arrays of size 2 of file descriptors
 */
void readFromPipes(int **pipes) {
    struct timeval tv;
    char buff[BUFF_SIZE], *timeBuff;
    fd_set readSet[numChildren];
    int i, selectVal, readVal;
    time_t startTime = 0;
    FILE *outputFile = fopen("output.txt", "w");
    if(outputFile == NULL) {
        fprintf(stderr, "Could not open output.txt with errno %d\n", errno);
        exit(errno);
    }

    tv.tv_sec = 4;
    tv.tv_usec = 0;
    startTime = time(0);
    gettimeofday(&startTV, NULL);

    while(time(0)-startTime < TIME_TO_RUN) {
        FD_ZERO(&readSet[i]);
        for(i = 0; i < numChildren; i++) {
            FD_SET(pipes[i][READ_PIPE], &readSet[i]);
            selectVal = select(pipes[i][READ_PIPE]+1, &readSet[i], NULL, NULL, &tv);
            tv.tv_sec = TV_WAIT_TIME;
            tv.tv_usec = 0;
            if(selectVal == -1) {
                fprintf(stderr, "select failed with errno %d\n", errno);
            } else if(selectVal) {
                // read from a pipe
                if((readVal = read(pipes[i][READ_PIPE], &buff, BUFF_SIZE)) > 0) {
                    timeBuff = getTime();
                    writeCarefully(timeBuff, buff, readVal, outputFile);
                    writeCarefully(timeBuff, buff, readVal, stdout);
                    memset(buff, 0, sizeof(char)*BUFF_SIZE);
                    free(timeBuff);
                    timeBuff = NULL;
                } else {
                    fprintf(stderr, "read failed with errno %d\n", errno);
                }
            } 
        } 
    }
    for(i = 0; i < numChildren; i++) {
        close(pipes[i][WRITE_PIPE]);
        close(pipes[i][READ_PIPE]);
    }
    fclose(outputFile);
}

/* periodically writes to the write pipe
 * @param pipe
 *      a reference to an int[2], which represents the read and write pipes
 * @param childNum
 *      which child the process that is running this function is
 */
void writeToPipe(int *pipe, int childNum) {
    time_t startTime;
    int sleepTime, messageNum = 0;
    char buff[BUFF_SIZE], *timeBuff;
    close(pipe[READ_PIPE]);
    srand(0);

    startTime = time(0);
    gettimeofday(&startTV, NULL);
    while(time(0)-startTime < TIME_TO_RUN) {
        messageNum++;
        sleepTime = rand()%(MAX_SLEEP_TIME+1);
        if(sleepTime != 0) {
            sleep(sleepTime);
        }
        timeBuff = getTime();
        snprintf(buff, BUFF_SIZE, "%s Child %d message %d\n", timeBuff, childNum, messageNum);
        write(pipe[WRITE_PIPE], buff, strlen(buff)+1);
        free(timeBuff);
        timeBuff = NULL;
    }

    close(pipe[WRITE_PIPE]);
}

/* takes input from the user on stdin and writes it to its write pipe
 * @param pipe
 *      a reference to an int[2], which represents the read and write pipes
 */
void lastChild(int *pipe) {
    ssize_t nread;
    time_t startTime = time(0);
    char *line = NULL;
    size_t alloced = 0;
    int messageNum = 0;
    char buff[BUFF_SIZE], *timeBuff;
    close(pipe[READ_PIPE]);

    startTime = time(0);
    gettimeofday(&startTV, NULL);
    while(time(0)-startTime < TIME_TO_RUN) {
        if((nread = getline(&line, &alloced, stdin)) != -1) {
            messageNum++;
            timeBuff = getTime();
            snprintf(buff, BUFF_SIZE, "%s %s\n", timeBuff, line);
            write(pipe[WRITE_PIPE], buff, strlen(buff+1));
            free(timeBuff);
            timeBuff = NULL;
        }
        free(line);
        line = NULL;
    }   
    close(pipe[WRITE_PIPE]);
}

/* makes the children processes and has them write to their pipes
 * the parent process will then read from the pipes and log the messages it 
 * recieves
 * @param pipes
 *      an array with numChild entries, which are arrays of size 2 of file descriptors
 */
void makeChildren(int **pipes) {
    int i, pids[numChildren];
    for(i = 0; i < numChildren; i++) {
        pids[i] = fork();
        if(i == numChildren-1 && pids[i] == 0) {
            lastChild(pipes[i]);
            return;
        } else {
            if(pids[i] == 0) {
                writeToPipe(pipes[i], i+1);               
                return;
            }
        }
    }
    readFromPipes(pipes);
    for(i = 0; i < numChildren; i++) {
        waitpid(pids[i], NULL, 0);
    }
}

int main(int argc, char *argv[]) {
    int **pipes = getPipes();

    makeChildren(pipes);
    
    freePipes(pipes);
}
