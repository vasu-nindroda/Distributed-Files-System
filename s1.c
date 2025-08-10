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

// Response codes
#define SUCCESS 0
#define ERROR_FILE_CREATE -1
#define ERROR_NETWORK -2
#define ERROR_MEMORY -3
#define ERROR_FILE_READ -4

char *server2_ip;
int server2_port;
char *server3_ip;
int server3_port;
char *server4_ip;
int server4_port;

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

int communicateWithServer(char* commandType, char *filePath, char *fileBuffer, int fileSize, char *sIp, int sPort, char *response, int main_clinet_sd){
    int client_sd;
    struct sockaddr_in server_addr;
    
    if((client_sd = socket(AF_INET, SOCK_STREAM, 0))<0){
    fprintf(stderr, "Could not create socket\n");
    exit(1);
    }
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(sPort);
    inet_pton(AF_INET, sIp, &server_addr.sin_addr);
    
    if(connect(client_sd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0){
        close(client_sd);
        return -1;
    }

    if(strcmp(commandType, "uploadf") == 0){
        //Send command first
        char command[MAX_BUFFER];
        snprintf(command, MAX_BUFFER, "uploadf %s", filePath);
        if(write(client_sd, command, strlen(command)) < 0){
            close(client_sd);
            strcpy(response, "Error: Failed to send command to server");
            return ERROR_NETWORK;
        }
        usleep(10000);
        //Send file size as network bytes
        uint32_t networkFileSize = htonl((uint32_t)fileSize);
        if(write(client_sd, &networkFileSize, sizeof(networkFileSize)) != sizeof(networkFileSize)) {
            close(client_sd);
            strcpy(response, "Error: Failed to send file size to Server");
            return ERROR_NETWORK;
        }
        //Send file data in chunk
        if(sendDataInChunks(client_sd, fileBuffer, fileSize) != fileSize){
            close(client_sd);
            strcpy(response, "Error: Failed to send file data to Server");
            return ERROR_NETWORK;
        }
        //Read response from server
        int responseLen = read(client_sd, response, MAX_BUFFER - 1);
        if(responseLen <= 0){
            strcpy(response, "Error: No response from Server");
            close(client_sd);
            return ERROR_NETWORK;
        }
        response[responseLen] = '\0';
        close(client_sd);
    }

    if(strcmp(commandType, "removef") == 0){
        char command[MAX_BUFFER];
        snprintf(command, MAX_BUFFER, "removef %s", filePath);
        if(write(client_sd, command, strlen(command)) < 0){
            close(client_sd);
            strcpy(response, "Error: Failed to send command to server");
            return ERROR_NETWORK;
        }

        //Read response from server
        int responseLen = read(client_sd, response, MAX_BUFFER - 1);
        if(responseLen <= 0){
            strcpy(response, "Error: No response from Server");
            close(client_sd);
            return ERROR_NETWORK;
        }
        response[responseLen] = '\0';
        close(client_sd);
    }

    if(strcmp(commandType, "downlf") == 0){
        char command[MAX_BUFFER];
        snprintf(command, MAX_BUFFER, "downlf %s", filePath);
        //Send Command
        if(write(client_sd, command, strlen(command)) < 0){
            close(client_sd);
            snprintf(response, MAX_BUFFER, "Error: Failed to send command to server");
            write(main_clinet_sd, response, strlen(response));
            return ERROR_NETWORK;
        }
        //Receive file size (convert from network byte order) from server
        uint32_t networkFileSize;
        int bytes = read(client_sd, &networkFileSize, sizeof(uint32_t));
        int fileSize = ntohl(networkFileSize);
        if(bytes <= 0 || fileSize <= 0 || fileSize > MAX_FILE_SIZE){
            close(client_sd);
            snprintf(response, MAX_BUFFER, "Error: Invalid file size");
            write(main_clinet_sd, response, strlen(response));
            return ERROR_NETWORK;
        }
        //Allocate memory for file
        char *fileData = malloc(fileSize + 1);
        if(!fileData){
            close(client_sd);
            snprintf(response, MAX_BUFFER, "Error: Memory allocation failed");
            write(main_clinet_sd, response, strlen(response));
            return ERROR_NETWORK;
        }
        //Receive File Data
        int totalReceived = receiveDataInChunks(client_sd, fileData, fileSize);
        if(totalReceived != fileSize){
            free(fileData);
            close(client_sd);
            snprintf(response, MAX_BUFFER, "Error: Failed to receive complete file data");
            write(main_clinet_sd, response, strlen(response));
            return ERROR_NETWORK;
        }
        //Send success response to client first
        snprintf(response, MAX_BUFFER, "Success: File retrieved from target server");
        if(write(main_clinet_sd, response, strlen(response)) <= 0) {
            free(fileData);
            return ERROR_NETWORK;
        }
        usleep(10000);
        //Send File Name to client
        char* lastSlash = strrchr(filePath, '/');
        char* fileName = (lastSlash == NULL) ? filePath : lastSlash + 1;
        if(write(main_clinet_sd, fileName, strlen(fileName)) <= 0){
            free(fileData);
            return ERROR_NETWORK;
        }
        usleep(10000);
        //Send File size to client
        uint32_t networkFileSizeClient = htonl((uint32_t)fileSize);
        if(write(main_clinet_sd, &networkFileSizeClient, sizeof(networkFileSizeClient)) != sizeof(networkFileSizeClient)){
            free(fileData);
            return ERROR_NETWORK;
        }
        usleep(10000);
        //Send file data in chunk to client
        if(sendDataInChunks(main_clinet_sd, fileData, fileSize) != fileSize){
            free(fileData);
            return ERROR_NETWORK;
        }
        free(fileData);
        close(client_sd);
    }
    return SUCCESS;
}

char* getFileExtension(char* filename){
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
    if(strstr(tempPath, "/S1/")){
        // Skip "/S1"
        baseEnd = strstr(tempPath, "/S1/") + 3; 
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

void handleUploadf(int con_sd, char* commandArgs[], int* count){
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s", commandArgs[*count - 1]);
    char destPath[MAX_PATH];
    strcpy(destPath, path);
    //Replace ~S1 with /home/user/S1
    if(strncmp(destPath, "~S1", 3) == 0){
        char* home = getenv("HOME");
        char temp[MAX_PATH];
        sprintf(temp, "%s/S1%s", home, destPath + 3);
        strcpy(destPath, temp);
    }
    if(createDirectory(destPath) == -1){
        char* errorMsg = "\nError: Failed to create directory on server.\n";
        write(con_sd, errorMsg, strlen(errorMsg));
        return;
    }
    // Store information about all files
    typedef struct{
        char filename[MAX_PATH];
        char filepath[MAX_PATH];
        char extension[32];
        char* fileBuffer;
        int fileSize;
    } FileInfo;
    FileInfo files[3];
    int numFiles = *count - 2;
    //Process each file
    for(int i = 0; i < numFiles; i++){
        strcpy(files[i].filename, commandArgs[i + 1]);
        snprintf(files[i].filepath, sizeof(files[i].filepath), "%s/%s", destPath, files[i].filename);
        strcpy(files[i].extension, getFileExtension(files[i].filename));
        // Read file size first
        if(read(con_sd, &files[i].fileSize, sizeof(int)) <= 0) {
            char* errorMsg = "\nError: Failed to receive file size.\n";
            write(con_sd, errorMsg, strlen(errorMsg));
            for(int j = 0; j < i; j++){
                if(files[j].fileBuffer){
                    free(files[j].fileBuffer);
                }
            }
            return;
        }
        // Validate file size
        if(files[i].fileSize <= 0 || files[i].fileSize > MAX_FILE_SIZE){
            char* errorMsg = "\nError: Invalid file size.\n";
            write(con_sd, errorMsg, strlen(errorMsg));
            for(int j = 0; j < i; j++){
                if(files[j].fileBuffer){
                    free(files[j].fileBuffer);
                }
            }
            return;
        }
        //Allocate buffer for file data
        files[i].fileBuffer = malloc(files[i].fileSize + 1);
        if(!files[i].fileBuffer) {
            char* errorMsg = "\nError: Memory allocation failed.\n";
            write(con_sd, errorMsg, strlen(errorMsg));
            for(int j = 0; j < i; j++){
                if(files[j].fileBuffer){
                    free(files[j].fileBuffer);
                }
            }
            return;
        }
        //Receive file data in chunks
        int bytesReceived = receiveDataInChunks(con_sd, files[i].fileBuffer, files[i].fileSize);
        
        if(bytesReceived != files[i].fileSize) {
            char* errorMsg = "\nError: Failed to receive complete file data.\n";
            write(con_sd, errorMsg, strlen(errorMsg));
            for(int j = 0; j < i; j++){
                if(files[j].fileBuffer){
                    free(files[j].fileBuffer);
                }
            }
            return;
        }
        //Store file on Server1 first
        int fd = open(files[i].filepath, O_CREAT | O_WRONLY, 0777);
        if(fd < 0){
            char* errorMsg = "\nError: Failed to create file on Server1.\n";
            write(con_sd, errorMsg, strlen(errorMsg));
            for(int j = 0; j <= i; j++){
                if(files[j].fileBuffer){
                    free(files[j].fileBuffer);
                }
            }
            return;
        }

        if(write(fd, files[i].fileBuffer, files[i].fileSize) != files[i].fileSize){
            close(fd);
            char* errorMsg = "\nError: Failed to write file on Server1.\n";
            write(con_sd, errorMsg, strlen(errorMsg));
            // Clean up all allocated buffers
            for(int j = 0; j <= i; j++){
                if(files[j].fileBuffer){
                     free(files[j].fileBuffer);
                }
            }
            return;
        }
        close(fd);
    }    
    // Now process each file based on extension
    for(int i = 0; i < numFiles; i++){
        char response[MAX_BUFFER];
        int result = SUCCESS;
        if(strcmp(files[i].extension, ".c") == 0){
            snprintf(response, sizeof(response), "File uploaded successfully to Server");
        }
        else if(strcmp(files[i].extension, ".pdf") == 0){
            char modifiedPath[MAX_PATH];
            strcpy(modifiedPath, files[i].filepath);
            char* s1_ptr = strstr(modifiedPath, "/S1/");
            //Change S1 to S2
            if(s1_ptr != NULL) {
                s1_ptr[2] = '2';  
            }
            result = communicateWithServer("uploadf", modifiedPath, files[i].fileBuffer, files[i].fileSize, server2_ip, server2_port, response, 0);
            // Remove file from Server1 if successfully sent to Server2
            if(result == SUCCESS) {
                unlink(files[i].filepath);
            }
        }
        else if(strcmp(files[i].extension, ".txt") == 0){
            char modifiedPath[MAX_PATH];
            strcpy(modifiedPath, files[i].filepath);
            char* s1_ptr = strstr(modifiedPath, "/S1/");
            //Change S1 to S2
            if(s1_ptr != NULL) {
                s1_ptr[2] = '3';  
            }
            result = communicateWithServer("uploadf", modifiedPath, files[i].fileBuffer, files[i].fileSize, server3_ip, server3_port, response, 0);
            // Remove file from Server1 if successfully sent to Server3
            if(result == SUCCESS) {
                unlink(files[i].filepath);
            }
        }
        else if(strcmp(files[i].extension, ".zip") == 0){
            char modifiedPath[MAX_PATH];
            strcpy(modifiedPath, files[i].filepath);
            char* s1_ptr = strstr(modifiedPath, "/S1/");
            if(s1_ptr != NULL) {
                s1_ptr[2] = '4';
            }
            result = communicateWithServer("uploadf", modifiedPath, files[i].fileBuffer, files[i].fileSize, server4_ip, server4_port, response, 0);
            // Remove file from Server1 if successfully sent to Server4
            if(result == SUCCESS) {
                unlink(files[i].filepath);
            }
        }
        write(con_sd, response, strlen(response));
        free(files[i].fileBuffer);
    }   
    rmdir(destPath); 
}

void handleDownlf(int con_sd, char* commandArgs[], int* count){
    //Loop through each path
    for(int i = 1; i < *count; i++){
        char response[MAX_BUFFER];
        char ext[32];
        snprintf(ext, sizeof(ext), "%s",  getFileExtension(commandArgs[i]));
        if(strcmp(ext, ".c") == 0){
            char path[MAX_PATH];
            snprintf(path, sizeof(path), "%s", commandArgs[i]);
            char destPath[MAX_PATH];
            strcpy(destPath, path);
            //Replace ~S1 with /home/user/S1
            if(strncmp(destPath, "~S1", 3) == 0){
                char* home = getenv("HOME");
                char temp[MAX_PATH];
                sprintf(temp, "%s/S1%s", home, destPath + 3);
                strcpy(destPath, temp);
            }
            if(!validateFileExist(destPath)){
                snprintf(response, sizeof(response), "Error: File does not exist on Server");
                write(con_sd, response, strlen(response));
                continue;
            }
            //Get file size
            struct stat st;
            stat(destPath, &st);
            int fileSize = st.st_size;
            char *fileBuffer = malloc(fileSize + 1);
            if(!fileBuffer) {
                snprintf(response, sizeof(response), "Error: Memory allocation failed");
                write(con_sd, response, strlen(response));
                continue;
            }
            //Read File Data locally
            int fd = open(destPath, O_RDONLY);
            if(fd < 0){
                free(fileBuffer);
                snprintf(response, sizeof(response), "Error: Failed to open file on server");
                write(con_sd, response, strlen(response));
                continue;
            }
            int bytesRead = read(fd, fileBuffer, fileSize);
            close(fd);
            if(bytesRead != fileSize){
                free(fileBuffer);
                snprintf(response, sizeof(response), "Error: Failed to read file on server");
                write(con_sd, response, strlen(response));
                continue;
            }
            //Send Success read to client
            snprintf(response, sizeof(response), "Success: File found and ready to transfer");
            if(write(con_sd, response, strlen(response)) <= 0){
                free(fileBuffer);
                continue;
            }
            usleep(10000);
            //Send File Name to client
            char* lastSlash = strrchr(destPath, '/');
            char* fileName = (lastSlash == NULL) ? destPath : lastSlash + 1;
            if(write(con_sd, fileName, strlen(fileName)) <= 0){
                free(fileBuffer);
                continue;
            }
            usleep(10000);
            //Send File size to client
            uint32_t networkFileSizeClient = htonl((uint32_t)fileSize);
            if(write(con_sd, &networkFileSizeClient, sizeof(networkFileSizeClient)) != sizeof(networkFileSizeClient)){
                strcpy(response, "Error: Failed to send file size");
                free(fileBuffer);
                continue;
            }
            usleep(10000);
            //Send file data in chunk to client
            if(sendDataInChunks(con_sd, fileBuffer, fileSize) != fileSize){
                strcpy(response, "Error: Failed to send file data to clinet");
                free(fileBuffer);
                continue;
            }
            free(fileBuffer);
        }
        else if(strcmp(ext, ".pdf") == 0){
            char path[MAX_PATH];
            snprintf(path, sizeof(path), "%s", commandArgs[i]);
            char destPath[MAX_PATH];
            strcpy(destPath, path);
            //Replace ~S1 with /home/user/S1
            if(strncmp(destPath, "~S1", 3) == 0){
                char* home = getenv("HOME");
                char temp[MAX_PATH];
                sprintf(temp, "%s/S1%s", home, destPath + 3);
                strcpy(destPath, temp);
            }
            char* s1_ptr = strstr(destPath, "/S1/");
            if(s1_ptr != NULL) {
                s1_ptr[2] = '2';
            }
            struct stat st;
            stat(destPath, &st);
            int fileSize = st.st_size;
            int result = communicateWithServer("downlf", destPath, NULL, fileSize, server2_ip, server2_port, response, con_sd);
            if(result != SUCCESS){
                return;
            }
        }
        else if(strcmp(ext, ".txt") == 0){
            char path[MAX_PATH];
            snprintf(path, sizeof(path), "%s", commandArgs[i]);
            char destPath[MAX_PATH];
            strcpy(destPath, path);
            //Replace ~S1 with /home/user/S1
            if(strncmp(destPath, "~S1", 3) == 0){
                char* home = getenv("HOME");
                char temp[MAX_PATH];
                sprintf(temp, "%s/S1%s", home, destPath + 3);
                strcpy(destPath, temp);
            }
            char* s1_ptr = strstr(destPath, "/S1/");
            if(s1_ptr != NULL) {
                s1_ptr[2] = '3';
            }
            struct stat st;
            stat(destPath, &st);
            int fileSize = st.st_size;
            int result = communicateWithServer("downlf", destPath, NULL, fileSize, server3_ip, server3_port, response, con_sd);
            if(result != SUCCESS){
                return;
            }
        }
        else if(strcmp(ext, ".zip") == 0){
            char path[MAX_PATH];
            snprintf(path, sizeof(path), "%s", commandArgs[i]);
            char destPath[MAX_PATH];
            strcpy(destPath, path);
            //Replace ~S1 with /home/user/S1
            if(strncmp(destPath, "~S1", 3) == 0){
                char* home = getenv("HOME");
                char temp[MAX_PATH];
                sprintf(temp, "%s/S1%s", home, destPath + 3);
                strcpy(destPath, temp);
            }
            char* s1_ptr = strstr(destPath, "/S1/");
            if(s1_ptr != NULL) {
                s1_ptr[2] = '4';
            }
            struct stat st;
            stat(destPath, &st);
            int fileSize = st.st_size;
            int result = communicateWithServer("downlf", destPath, NULL, fileSize, server4_ip, server4_port, response, con_sd);
            if(result != SUCCESS){
                return;
            }
        }
        else{
            snprintf(response, sizeof(response), "Error: Invalid extension");
            write(con_sd, response, strlen(response));
        }
    }
}

void handleRemovef(int con_sd, char* commandArgs[], int* count){
    //Loop through each path
    for(int i = 1; i < *count; i++){
        char response[MAX_BUFFER];
        char ext[32];
        snprintf(ext, sizeof(ext), "%s",  getFileExtension(commandArgs[i]));
        if(strcmp(ext, ".c") == 0){
            char path[MAX_PATH];
            snprintf(path, sizeof(path), "%s", commandArgs[i]);
            char destPath[MAX_PATH];
            strcpy(destPath, path);
            //Replace ~S1 with /home/user/S1
            if(strncmp(destPath, "~S1", 3) == 0){
                char* home = getenv("HOME");
                char temp[MAX_PATH];
                sprintf(temp, "%s/S1%s", home, destPath + 3);
                strcpy(destPath, temp);
            }
            if(!validateFileExist(destPath)){
                snprintf(response, sizeof(response), "File does not exist on Server");
                break;
            }
            unlink(destPath);
            snprintf(response, sizeof(response), "File removed successfully from Server");
        }
        else if(strcmp(ext, ".pdf") == 0){
            char path[MAX_PATH];
            snprintf(path, sizeof(path), "%s", commandArgs[i]);
            char destPath[MAX_PATH];
            strcpy(destPath, path);
            //Replace ~S1 with /home/user/S1
            if(strncmp(destPath, "~S1", 3) == 0){
                char* home = getenv("HOME");
                char temp[MAX_PATH];
                sprintf(temp, "%s/S1%s", home, destPath + 3);
                strcpy(destPath, temp);
            }
            char* s1_ptr = strstr(destPath, "/S1/");
            if(s1_ptr != NULL) {
                s1_ptr[2] = '2';
            }
            communicateWithServer("removef", destPath, NULL, 0, server2_ip, server2_port, response, 0);
        }
        else if(strcmp(ext, ".txt") == 0){
            char path[MAX_PATH];
            snprintf(path, sizeof(path), "%s", commandArgs[i]);
            char destPath[MAX_PATH];
            strcpy(destPath, path);
            //Replace ~S1 with /home/user/S1
            if(strncmp(destPath, "~S1", 3) == 0){
                char* home = getenv("HOME");
                char temp[MAX_PATH];
                sprintf(temp, "%s/S1%s", home, destPath + 3);
                strcpy(destPath, temp);
            }
            char* s1_ptr = strstr(destPath, "/S1/");
            if(s1_ptr != NULL) {
                s1_ptr[2] = '3';
            }
            communicateWithServer("removef", destPath, NULL, 0, server3_ip, server3_port, response, 0);
        }
        else if(strcmp(ext, ".zip") == 0){
            char path[MAX_PATH];
            snprintf(path, sizeof(path), "%s", commandArgs[i]);
            char destPath[MAX_PATH];
            strcpy(destPath, path);
            //Replace ~S1 with /home/user/S1
            if(strncmp(destPath, "~S1", 3) == 0){
                char* home = getenv("HOME");
                char temp[MAX_PATH];
                sprintf(temp, "%s/S1%s", home, destPath + 3);
                strcpy(destPath, temp);
            }
            char* s1_ptr = strstr(destPath, "/S1/");
            if(s1_ptr != NULL) {
                s1_ptr[2] = '4';
            }
            communicateWithServer("removef", destPath, NULL, 0, server4_ip, server4_port, response, 0);
        }
        else{
            snprintf(response, sizeof(response), "Error: Invalid extension");
        }
        write(con_sd, response, strlen(response));
    }
}

void handleDownltar(int con_sd, char* commandArgs[], int* count){

}

void handleDispfnames(int con_sd, char* commandArgs[], int* count){

}

void prcclient(int con_sd){
    char command[MAX_BUFFER];
    char* commandArgs[MAX_COMMAND_ARGS];
    int bytes;
    while(1){
        int count = 0;
        memset(command, 0, MAX_BUFFER);
        bytes = read(con_sd, command, MAX_BUFFER - 1);
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
            char* errorMsg = "Error: Command tokenization failed.\n";
            write(con_sd, errorMsg, strlen(errorMsg));
            break;
        }
        if(strcmp(commandArgs[0], "uploadf") == 0){
            handleUploadf(con_sd, commandArgs, &count);
        } 
        else if(strcmp(commandArgs[0], "downlf") == 0){
            handleDownlf(con_sd, commandArgs, &count);
        } 
        else if(strcmp(commandArgs[0], "removef") == 0){
            handleRemovef(con_sd, commandArgs, &count);
        } 
        else if(strcmp(commandArgs[0], "downltar") == 0){
            handleDownltar(con_sd, commandArgs, &count);
        } 
        else if(strcmp(commandArgs[0], "dispfnames") == 0){
            handleDispfnames(con_sd, commandArgs, &count);
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

    if(argc != 8){
        fprintf(stderr, "Usage: %s <Server1_Port> <Server2_IP> <Server2_Port> <Server3_IP> <Server3_Port> <Server4_IP> <Server4_Port>\n", argv[0]);
        exit(0);
    }

    server2_ip = argv[2];
    sscanf(argv[3], "%d", &server2_port);
    server3_ip = argv[4];
    sscanf(argv[5], "%d", &server3_port);
    server4_ip = argv[6];
    sscanf(argv[7], "%d", &server4_port);

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
            prcclient(con_sd);
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
