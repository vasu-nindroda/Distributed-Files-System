#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

#define MAX_BUFFER 2048
#define MAX_PATH 512
#define MAX_COMMAND_ARGS 5
#define CHUNK_SIZE 8192
#define MAX_FILE_SIZE (50 * 1024 * 1024)

//Const is used to ensure no modification is made to data
int sendDataInChunks(int socket, const char *data, int dataSize){
    int totalSent = 0;
    int bytesToSend;
    
    while(totalSent < dataSize){
        bytesToSend = (dataSize - totalSent > CHUNK_SIZE) ? CHUNK_SIZE : (dataSize - totalSent);
        int sent = write(socket, data + totalSent, bytesToSend);
        if(sent <= 0){
            return -1;
        }
        totalSent += sent;
    }
    return totalSent;
}

int receiveDataInChunks(int socket, char *buffer, int expectedSize){
    int totalReceived = 0;
    int bytesToReceive;
    
    while(totalReceived < expectedSize){
        bytesToReceive = (expectedSize - totalReceived > CHUNK_SIZE) ? CHUNK_SIZE : (expectedSize - totalReceived);
        int received = read(socket, buffer + totalReceived, bytesToReceive);
        if(received <= 0){
            return -1;
        }
        totalReceived += received;
    }
    return totalReceived;
}

void extractPath(char* path) {
    char* lastSlash = strrchr(path, '/');
    //Treminate the string at the last slash to remove file name
    if (lastSlash != NULL) {
        *lastSlash = '\0'; 
    }
}

int validateFileExist(char *filename) {
    struct stat st;
    if (stat(filename, &st) != 0) {
        perror(filename);
        return 0;
    }
    return 1;
}

int tokenizeCommand(char* input, char* commandArgs[], int* count){
    char copyInput[MAX_BUFFER];
    strcpy(copyInput, input);
    char* delimiter = " \t";
    //Split on the base of delimiter using strtok
    char* portion = strtok(copyInput, delimiter);
    while(portion != NULL){
        commandArgs[*count] = malloc(strlen(portion) + 1);
        if(commandArgs[*count] == NULL){
            printf("\nError: Memory allocation failed.\n");
            return 0;
        }
        //Copy the split portion to commandArgs[]
        strcpy(commandArgs[*count], portion);
        (*count)++;
        portion = strtok(NULL, delimiter);
    }
    return 1;
}

int createDirectory(char* path){
    char tempPath[MAX_PATH];
    char* p = NULL;
    int len;
    //Copy the input path
    snprintf(tempPath, sizeof(tempPath), "%s", path);
    len = strlen(tempPath);
    //Remove trailing slash
    if(tempPath[len - 1] == '/'){
        tempPath[len - 1] = 0;
    }
    char* baseEnd = NULL;
    if(strstr(tempPath, "/S3/")){
        // Skip "/S3"
        baseEnd = strstr(tempPath, "/S3/") + 3; 
    } 
    //Iterate through the path and create subdirectories
    for(p = baseEnd; *p; p++){
        if (*p == '/') {
            *p = 0;
            int result = mkdir(tempPath, 0755);
            if(result == -1 && errno != EEXIST){
                return -1;
            }
            *p = '/';
        }
    }
    int result = mkdir(tempPath, 0755);
    if(result == -1 && errno != EEXIST){
        return -1;
    }
    
    return 0;
}

void handleUploadf(int con_sd, char* commandArgs[]){
    //Get File path and name
    char fileCommand[256];
    snprintf(fileCommand, sizeof(fileCommand), "%s", commandArgs[1]);   
    // Read File size (convert from network byte order)
    uint32_t networkFileSize;
    int bytes = read(con_sd, &networkFileSize, sizeof(uint32_t));
    int fileSize = ntohl(networkFileSize);
    if(bytes <= 0 || fileSize <= 0 || fileSize > MAX_FILE_SIZE){
        char* errorMsg = "Error: Invalid file size server";
        write(con_sd, errorMsg, strlen(errorMsg));
        return;
    }
    //Allocate memory for file
    char *fileData = malloc(fileSize + 1);
    if(!fileData){
        char* errorMsg = "Error: Memory allocation failed";
        write(con_sd, errorMsg, strlen(errorMsg));
        return;
    }
    //Variable to store file name and path
    char destPath[MAX_PATH];
    char filePathAndName[MAX_PATH];
    memset(filePathAndName, 0, sizeof(filePathAndName));
    strcpy(filePathAndName, fileCommand);
    memset(destPath, 0, sizeof(destPath));
    strcpy(destPath, fileCommand);
    //Extract only the path from destPath
    extractPath(destPath);
    if(createDirectory(destPath) == -1){
        free(fileData);
        char* errorMsg = "\nError: Failed to create directory on server.\n";
        write(con_sd, errorMsg, strlen(errorMsg));
        return;
    }
    //Receive file data in chunks
    int totalReceived = receiveDataInChunks(con_sd, fileData, fileSize);
    if(totalReceived != fileSize){
        free(fileData);
        char* errorMsg = "Error: Failed to receive complete file data";
        write(con_sd, errorMsg, strlen(errorMsg));
        return;
    }
    //Create and write file
    int fd = open(filePathAndName, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if(fd < 0){
        free(fileData);
        char* errorMsg = "Error: Failed to create file on Server2";
        write(con_sd, errorMsg, strlen(errorMsg));
        return;
    }
    
    int bytesWritten = write(fd, fileData, fileSize);
    close(fd);
    free(fileData);
    
    if(bytesWritten != fileSize) {
        char* errorMsg = "Error: Failed to write complete file on Server2";
        write(con_sd, errorMsg, strlen(errorMsg));
        return;
    }
    
    // Send success response
    char successMsg[MAX_BUFFER];
    snprintf(successMsg, sizeof(successMsg), "File uploaded successfully to Server");
    write(con_sd, successMsg, strlen(successMsg));
}

void handleDownlf(int con_sd, char* commandArgs[]){
    char response[MAX_BUFFER];
    //Get file size info
    struct stat st;
    stat(commandArgs[1], &st);
    int fileSize = st.st_size;
    char* fileBuffer = malloc(fileSize + 1);
    if(!validateFileExist(commandArgs[1])){
        snprintf(response, sizeof(response), "Error: File does not exist on Server");
        write(con_sd, response, strlen(response));
        return;
    }
    //Read File Data locally
    int fd = open(commandArgs[1], O_RDONLY);
    if(fd < 0){
        free(fileBuffer);
        snprintf(response, sizeof(response), "Error: Failed to open file on server");
        write(con_sd, response, strlen(response));
        return;
    }
    int bytesRead = read(fd, fileBuffer, fileSize);
    close(fd);
    if(bytesRead != fileSize){
        free(fileBuffer);
        snprintf(response, sizeof(response), "Error: Failed to read file on server");
        write(con_sd, response, strlen(response));
        return;
    }
    //Send file size to server 1
    uint32_t networkFileSize = htonl((uint32_t)fileSize);
    if(write(con_sd, &networkFileSize, sizeof(networkFileSize)) != sizeof(networkFileSize)){
        free(fileBuffer);
        return;
    }
    usleep(10000);
    //Send file data in chunk to server 1
    if(sendDataInChunks(con_sd, fileBuffer, fileSize) != fileSize){
        free(fileBuffer);
        return;
    }
    free(fileBuffer);
}

void handleRemovef(int con_sd, char* commandArgs[]){
    char response[MAX_BUFFER];
    if(!validateFileExist(commandArgs[1])){
        snprintf(response, sizeof(response), "File does not exist on Server");
        write(con_sd, response, strlen(response));
        return;
    }
    unlink(commandArgs[1]);
    snprintf(response, sizeof(response), "File removed successfully from Server");
    write(con_sd, response, strlen(response));
}

void handleRequest(int con_sd){
    char command[MAX_BUFFER];
    char* commandArgs[MAX_COMMAND_ARGS];
    int bytes;
    while(1){
        int count = 0;
        memset(command, 0, MAX_BUFFER);
        bytes = read(con_sd, command, MAX_BUFFER);
        //Error if read fails
        if(bytes < 0){
            printf("\nClient Disconnected (read error).\n");
            break;
        }
        if(bytes == 0){
            printf("\nClient Disconnected (connection closed).\n");
            break; 
        }
        //Add string terminator
        command[bytes] = '\0';
        if(!tokenizeCommand(command, commandArgs, &count)){
            char* errorMsg = "Error: Command tokenization failed";
            write(con_sd, errorMsg, strlen(errorMsg));
            break;
        }
        if(strcmp(commandArgs[0], "uploadf") == 0){
            handleUploadf(con_sd, commandArgs);
        } 
        else if(strcmp(commandArgs[0], "downlf") == 0){
            handleDownlf(con_sd, commandArgs);
        } 
        else if(strcmp(commandArgs[0], "removef") == 0){
            handleRemovef(con_sd, commandArgs);
        } 
        else if(strcmp(commandArgs[0], "downltar") == 0){
            // handleDownltar(con_sd, commandArgs);
        } 
        else if(strcmp(commandArgs[0], "dispfnames") == 0){
            // handleDispfnames(con_sd, commandArgs);
        }
        for (int i = 0; i < count; i++) {
            if (commandArgs[i]) {
                free(commandArgs[i]);
                commandArgs[i] = NULL;
            }
        }
    }
    for (int i = 0; i < MAX_COMMAND_ARGS; i++) {
        if (commandArgs[i]) {
            free(commandArgs[i]);
        }
    }
    close(con_sd);
}

int main(int argc, char *argv[]){
    int lis_sd, con_sd, portNumber;
    socklen_t len;
    struct sockaddr_in servAdd;
    int pid;

    if(argc != 2){
        fprintf(stderr, "Usage: %s <Port>\n", argv[0]);
        exit(0);
    }

    //socket() sytem call
    if((lis_sd = socket(AF_INET, SOCK_STREAM, 0))<0){
    fprintf(stderr, "Could not create socket\n");
    exit(1);
    }

    //Add port number and IP address to servAdd before invoking the bind() system call
    servAdd.sin_family = AF_INET;
    //Add the IP address of the machine
    servAdd.sin_addr.s_addr = htonl(INADDR_ANY);
    //htonl: Host to Network Long : Converts host byte order to network byte order
    sscanf(argv[1], "%d", &portNumber);
    servAdd.sin_port = htons((uint16_t)portNumber);//Add the port number entered by the user

    //bind() system call
    bind(lis_sd, (struct sockaddr *) &servAdd,sizeof(servAdd));
    //listen max 5 client
    listen(lis_sd, 5);

    while(1){
        con_sd=accept(lis_sd,(struct sockaddr*)NULL,NULL);
        pid = fork();
        if(pid == 0){
            close(lis_sd);
            handleRequest(con_sd);
            exit(0);
        }
        else if(pid > 0){
            close(con_sd);
        }
        else{
            perror("\nFork Failed.\n");
        }
    }

    close(lis_sd);
    return 0;
}
