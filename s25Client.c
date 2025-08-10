#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <stdbool.h>


#define MAX_BUFFER 2048
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

//Function to remove spaces from start and end
void trim(char* str){
    int start = 0, end = strlen(str) - 1;
    while(str[start] == ' '){
        start++;
    }
    while(end > start && str[end] == ' '){
        end--;
    }
    int index = 0;
    while(start <= end){
        str[index] = str[start];
        index++;
        start++;
    }
    //Add string terminator
    str[index] = '\0';
}

char* getFileExtension(char* filename) {
    char* dot = strrchr(filename, '.');
    if(!dot || dot == filename){
        return "";
    }
    return dot;
}

int validateFileExist(char *filename) {
    struct stat st;
    if (stat(filename, &st) != 0) {
        perror(filename);
        return 0;
    }
    return 1;
}

//Helper function to validate file extension
int isValidExtension(char* filename, char* allowedExts[], int numExts) {
    char ext[32];
    snprintf(ext, sizeof(ext), "%s",  getFileExtension(filename));
    if(strlen(ext) == 0){
        return 0;
    } // No extension found
    
    for(int i = 0; i < numExts; i++){
        if(strcmp(ext, allowedExts[i]) == 0){
            return 1;
        }
    }
    return 0;
}

int isValidPath(char* command){
    //Must begin with ~S1
    if(!(strncmp(command, "~S1", 3) == 0)){
        return 0;
    }
    return 1;
}

//Function to read input from user
int readInput(char* input, int maxLen){
    //Using read system call and the FD is STD INPUT
    int bytes = read(STDIN_FILENO, input, maxLen - 1);
    //Error if read fails
    if(bytes < 0){
        printf("\nRead input failed.\n");
        exit(1);
    }
    //Add string terminator
    input[bytes] = '\0';
    int len = strlen(input);
    //Remove trailing newline character if present
    if(len > 0 && input[len - 1] == '\n'){
        input[len - 1] = '\0';
    }
    //If user has entered only space, return 1
    for(int i = 0; i < strlen(input); i++){
        if(input[i] != ' ' && input[i] != '\t'){
            return 1;
        }
    }
    //Return 0 for successful read
    return 0;
}

int validateCommandSyntax(char* input, char* commandArgs[], int* count){
    trim(input);
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
    //Define allowed extensions
    char* uploadfExts[] = {".c", ".pdf", ".txt", ".zip"};
    char* downltarExts[] = {".c", ".pdf", ".txt"};
    
    //Use if-else instead of switch for string comparison
    if(strcmp(commandArgs[0], "uploadf") == 0){
        if(*count < 3 || *count > 5){
            return 0;
        }
        int numFiles = *count - 2;
        if(numFiles < 1 || numFiles > 3){
            printf("\nError: uploadf requires 1 to 3 files\n");
            return 0;
        }
        char lastArg[256];
        snprintf(lastArg, sizeof(lastArg), "%s", commandArgs[*count - 1]);
        if(!isValidPath(lastArg)) {
            printf("\nError: Last argument must be a valid destination path.\n");
            return 0;
        }
        //Validate file extension for uploadf 
        for(int i = 1; i < *count - 1; i++){
            if(!isValidExtension(commandArgs[i], uploadfExts, 4)){
                printf("\nError: Invalid file extension for uploadf.\n");
                return 0;
            }
        }
    }
    else if(strcmp(commandArgs[0], "downlf") == 0){
        if(*count < 2 || *count > 3){
            return 0;
        }
        for(int i = 1; i < *count; i++){
            if(!isValidPath(commandArgs[i])) {
                printf("\nError: Command argument must be a valid path.\n");
                return 0;
            }
        }
    }
    else if(strcmp(commandArgs[0], "removef") == 0){
        if(*count < 2 || *count > 3){
            return 0;
        }
        for(int i = 1; i < *count; i++){
            if(!isValidPath(commandArgs[i])) {
                printf("\nError: Command argument must be a valid path.\n");
                return 0;
            }
        }
    }
    else if(strcmp(commandArgs[0], "downltar") == 0){
        if(*count == 1 || *count > 2){
            return 0;
        }
        char* ext = commandArgs[1];
        if(ext[0] != '.'){
            printf("\nError: downltar requires an extension.\n");
            return 0;
        }
        int validExt = 0;
        for(int i = 0; i < 3; i++){
            if(strcmp(ext, downltarExts[i]) == 0){
                validExt = 1;
                break;
            }
        }
        if(!validExt){
            printf("\nError: Invalid extension for downltar.\n");
            return 0;
        }
    }
    else if (strcmp(commandArgs[0], "dispfnames") == 0){
        if(*count > 2){
            return 0;
        }
    }
    else{
        return 0;
    }
    return 1;
}

int main(int argc, char *argv[]){
    char input[MAX_BUFFER];
    char* commandArgs[MAX_COMMAND_ARGS];
    int client_sd, portNumber;
    struct sockaddr_in servAdd;

    if(argc != 3){
        printf("Call model:%s <IP> <Port#>\n",argv[0]);
        exit(1);
    }

    if ((client_sd=socket(AF_INET,SOCK_STREAM,0))<0){
        fprintf(stderr, "\nError: Cannot create socket\n");
        exit(1);
    }

    //ADD the server's PORT NUMBER AND IP ADDRESS TO THE sockaddr_in object
    servAdd.sin_family = AF_INET;
    sscanf(argv[2], "%d", &portNumber);
    servAdd.sin_port = htons((uint16_t)portNumber);

    //inet_pton() is used to convert the IP address in text into binary
    if(inet_pton(AF_INET, argv[1],&servAdd.sin_addr) < 0){
        fprintf(stderr, "\nError: inet_pton() has failed\n");
        exit(1);
    }

    if(connect(client_sd, (struct sockaddr  *) &servAdd,sizeof(servAdd))<0){//Connect()
        fprintf(stderr, "\nError: connect() failed, exiting\n");
        exit(1);
    }

    printf("\nConnected to server\n");
    printf("\nAvailable commands:\n");
    printf("\n1. uploadf [filename1] [filename2] [filename3] destination_path\n");
    printf("\n2. downlf [filename1_path] [filename2_path]\n");
    printf("\n3. removef [filename1_path] [filename2_path]\n");
    printf("\n4. downltar filetype\n");
    printf("\n5. dispfnames pathname\n");
    printf("\nType 'quit' to exit\n");

    while(1){
        int count = 0;
        printf("s25client$ ");
        fflush(stdout);
        //Clearing buffer of commandArgs
        memset(commandArgs, 0, sizeof(commandArgs));
        //Calling readInput to read user input, and proceeding if valid input
        if(!readInput(input, MAX_BUFFER)){
            continue;
        }
        //If user enter quit, then terminate the code
        if(strcmp(input, "quit") == 0){
            break;
        }
        //Validate command syntax
        if (!validateCommandSyntax(input, commandArgs, &count)) {
            printf("\nError: Invalid command syntax\n");
            for(int i = 0; i < count; i++) {
                if(commandArgs[i]) {
                    free(commandArgs[i]);
                    commandArgs[i] = NULL;
                }
            }
            continue;
        }
        char commandToSend[MAX_BUFFER];
        strcpy(commandToSend, input);
        if(strcmp(commandArgs[0], "uploadf") == 0){
            for (int i = 1; i < count - 1; i++) {
                if (!validateFileExist(commandArgs[i])) {
                    continue;
                }
            }
            if(write(client_sd, input, strlen(input)) <= 0) {
                printf("\nError: Failed to send command to server\n");
                for(int i = 0; i < count; i++) {
                    if(commandArgs[i]) {
                        free(commandArgs[i]);
                        commandArgs[i] = NULL;
                    }
                }
                continue;
            }
            //Send each file
            for (int i = 1; i < count - 1; i++) {
                struct stat st;
                stat(commandArgs[i], &st);
                int fileSize = st.st_size;
                //Send file size first
                if(write(client_sd, &fileSize, sizeof(int)) <= 0) {
                    printf("\nError: Failed to send file size for '%s'\n", commandArgs[i]);
                    break;
                }
                // Allocate buffer for file
                char *fileBuffer = malloc(fileSize + 1);
                if(!fileBuffer) {
                    printf("\nError: Memory allocation failed for '%s'\n", commandArgs[i]);
                    break;
                }
                int fd = open(commandArgs[i], O_RDONLY);
                if(fd < 0){
                    printf("\nError: Failed to open file: %s\n", commandArgs[i]);
                    free(fileBuffer);
                    break;
                }
                int bytesRead = read(fd, fileBuffer, fileSize);
                close(fd);
                if(bytesRead != fileSize){
                    printf("\nError: Failed to read file: %s\n", commandArgs[i]);
                    free(fileBuffer);
                    break;
                }
                // Send file data in chunks
                int sentBytes = sendDataInChunks(client_sd, fileBuffer, fileSize);
                if(sentBytes != fileSize){
                    printf("\nError: Failed to send file '%s'\n", commandArgs[i]);
                    free(fileBuffer);
                    break;
                }
                free(fileBuffer);   
            }
            // Receive response
            for (int i = 1; i < count - 1; i++){
                char response[MAX_BUFFER];
                int responseLen = read(client_sd, response, MAX_BUFFER - 1);
                if (responseLen <= 0) {
                    printf("Failed to receive response for file %d\n", i);
                    break;
                }
                response[responseLen] = '\0';
                printf("Server response for file %d: %s\n", i, response);
            }
        } 
        else if(strcmp(commandArgs[0], "downlf") == 0){
            if(write(client_sd, input, strlen(input)) <= 0) {
                printf("\nError: Failed to send command to server\n");
                for(int i = 0; i < count; i++) {
                    if(commandArgs[i]) {
                        free(commandArgs[i]);
                        commandArgs[i] = NULL;
                    }
                }
                continue;
            }
            //For Each file get response and write file, loop through 1 as first element is downlf
            for (int i = 1; i < count; i++){
                //Get initial response
                char response[MAX_BUFFER];
                int responseLen = read(client_sd, response, MAX_BUFFER - 1);
                if (responseLen <= 0) {
                    printf("\nError: No response from server\n");
                    for(int i = 0; i < count; i++) {
                        if(commandArgs[i]) {
                            free(commandArgs[i]);
                            commandArgs[i] = NULL;
                        }
                    }
                    continue;
                }
                response[responseLen] = '\0';
                if(strstr(response, "Error:") != NULL || strstr(response, "File does not exist") != NULL) {
                    printf("%s\n", response);
                    for(int i = 0; i < count; i++) {
                        if(commandArgs[i]) {
                            free(commandArgs[i]);
                            commandArgs[i] = NULL;
                        }
                    }
                    continue;
                }
                //Read File Name
                char fileName[512];
                memset(fileName, 0, sizeof(fileName));
                if(read(client_sd, fileName, sizeof(fileName)) < 0){
                    printf("Failed to read file name from server.\n");
                    continue;
                }
                //Receive File Size from server
                uint32_t networkFileSize;
                int bytes = read(client_sd, &networkFileSize, sizeof(uint32_t));
                int fileSize = ntohl(networkFileSize);
                if(bytes <= 0 || fileSize <= 0 || fileSize > MAX_FILE_SIZE){
                    char* errorMsg = "Error: Invalid file size";
                    printf("\n%s\n", errorMsg);
                    for(int i = 0; i < count; i++) {
                        if(commandArgs[i]) {
                            free(commandArgs[i]);
                            commandArgs[i] = NULL;
                        }
                    }
                    continue;
                }
                //Allocate memory for file
                char *fileData = malloc(fileSize + 1);
                if(!fileData){
                    char* errorMsg = "Error: Memory allocation failed";
                    printf("\n%s\n", errorMsg);
                    for(int i = 0; i < count; i++) {
                        if(commandArgs[i]) {
                            free(commandArgs[i]);
                            commandArgs[i] = NULL;
                        }
                    }
                    continue;
                }
                //Receive File Data
                int totalReceived = receiveDataInChunks(client_sd, fileData, fileSize);
                if(totalReceived != fileSize){
                    free(fileData);
                    char* errorMsg = "Error: Failed to receive complete file data";
                    printf("\n%s\n", errorMsg);
                    for(int i = 0; i < count; i++) {
                        if(commandArgs[i]) {
                            free(commandArgs[i]);
                            commandArgs[i] = NULL;
                        }
                    }
                    continue;
                }
                //Create and write file On client
                int fd = open(fileName, O_CREAT | O_WRONLY | O_TRUNC, 0644);
                if(fd < 0){
                    free(fileData);
                    char* errorMsg = "Error: Failed to create file on Server2";
                    printf("\n%s\n", errorMsg);
                    for(int i = 0; i < count; i++) {
                        if(commandArgs[i]) {
                            free(commandArgs[i]);
                            commandArgs[i] = NULL;
                        }
                    }
                    continue;
                }
                //Check file written completely or not
                int bytesWritten = write(fd, fileData, fileSize);
                close(fd);
                free(fileData);
                if(bytesWritten != fileSize) {
                    char* errorMsg = "Error: Failed to write complete file on Server2";
                    printf("\n%s\n", errorMsg);
                    unlink(fileName);
                    continue;
                }
                printf("File %s downloaded successfully\n", fileName);
            }
        } 
        else if(strcmp(commandArgs[0], "removef") == 0){
            if(write(client_sd, input, strlen(input)) <= 0) {
                printf("\nError: Failed to send command to server\n");
                for(int i = 0; i < count; i++) {
                    if(commandArgs[i]) {
                        free(commandArgs[i]);
                        commandArgs[i] = NULL;
                    }
                }
                continue;
            }
            //Read reponse for each file path
            for(int i = 1; i < count; i++) {
                char response[MAX_BUFFER];
                int responseLen = read(client_sd, response, MAX_BUFFER - 1);
                if (responseLen <= 0) {
                    printf("Failed to receive response for file %d\n", i);
                    break;
                }
                response[responseLen] = '\0';
                printf("Server response for file %d: %s\n", i, response);
            }
        } 
        else if(strcmp(commandArgs[0], "downltar") == 0){
        } 
        else if(strcmp(commandArgs[0], "dispfnames") == 0){
        }
        for (int i = 0; i < MAX_COMMAND_ARGS; i++) {
            if (commandArgs[i]) {
                free(commandArgs[i]);
                commandArgs[i] = NULL;
            }
        }
    }
    close(client_sd);
    return 0;
}

