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

//Global constant
#define MAX_BUFFER 2048
#define MAX_PATH 512
#define MAX_COMMAND_ARGS 5
#define CHUNK_SIZE 8192
#define MAX_FILE_SIZE (50 * 1024 * 1024)

//Response codes
#define SUCCESS 0
#define ERROR_NETWORK -2

//Global variable for server 2-4 communication
char *server2_ip;
int server2_port;
char *server3_ip;
int server3_port;
char *server4_ip;
int server4_port;

//Helper function to send data in parts
int sendDataInChunks(int socket, const char *data, int dataSize){
    int totalSent = 0;
    int bytesToSend;
    //Write to socket till all data is not sent
    while(totalSent < dataSize){
        bytesToSend = (dataSize - totalSent > CHUNK_SIZE) ? CHUNK_SIZE : (dataSize - totalSent);
        int sent = write(socket, data + totalSent, bytesToSend);
        //Return -1, if write to socket fails
        if(sent <= 0){
            return -1;
        }
        totalSent += sent;
    }
    return totalSent;
}

//Helper function to receive data in parts
int receiveDataInChunks(int socket, char *buffer, int expectedSize){
    int totalReceived = 0;
    int bytesToReceive;
    //Read from socket till all data is not fetch
    while(totalReceived < expectedSize){
        bytesToReceive = (expectedSize - totalReceived > CHUNK_SIZE) ? CHUNK_SIZE : (expectedSize - totalReceived);
        int received = read(socket, buffer + totalReceived, bytesToReceive);
        //Return -1, if read from socket fails
        if(received <= 0){
            return -1;
        }
        totalReceived += received;
    }
    return totalReceived;
}

//Function to communicate with other server using server_port and server_ip
int communicateWithServer(char* commandType, char *filePath, char *fileBuffer, int fileSize, char *sIp, int sPort, char *response, int main_clinet_sd){
    //Socket variable
    int client_sd;
    struct sockaddr_in server_addr;
    //Socket call
    if((client_sd = socket(AF_INET, SOCK_STREAM, 0))<0){
    fprintf(stderr, "Could not create socket\n");
    exit(1);
    }
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(sPort);
    inet_pton(AF_INET, sIp, &server_addr.sin_addr);
    //Connect call to connect with other server
    if(connect(client_sd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0){
        close(client_sd);
        return -1;
    }
    //If command is uploadf
    if(strcmp(commandType, "uploadf") == 0){
        //Frist send the command to server using write
        char command[MAX_BUFFER];
        snprintf(command, MAX_BUFFER, "uploadf %s", filePath);
        if(write(client_sd, command, strlen(command)) < 0){
            //If write fails, close connection and send error message
            close(client_sd);
            strcpy(response, "Error: Failed to send command to server");
            return ERROR_NETWORK;
        }
        //Sleep for 10ms
        usleep(10000);
        //Use htonl to convert host bytes to network bytes
        uint32_t networkFileSize = htonl((uint32_t)fileSize);
        //Send file size to server using write
        if(write(client_sd, &networkFileSize, sizeof(networkFileSize)) != sizeof(networkFileSize)) {
            close(client_sd);
            strcpy(response, "Error: Failed to send file size to Server");
            return ERROR_NETWORK;
        }
        //Send file data in chunk
        if(sendDataInChunks(client_sd, fileBuffer, fileSize) != fileSize){
            //Error if all data is not sent
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
        //Close the connection
        close(client_sd);
    }
    //If command is removef
    if(strcmp(commandType, "removef") == 0){
        char command[MAX_BUFFER];
        snprintf(command, MAX_BUFFER, "removef %s", filePath);
        //Frist send the command to server using write
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
        //Close the connection
        close(client_sd);
    }
    //If command is downlf
    if(strcmp(commandType, "downlf") == 0){
        char command[MAX_BUFFER];
        snprintf(command, MAX_BUFFER, "downlf %s", filePath);
        //Frist send the command to server using write
        if(write(client_sd, command, strlen(command)) < 0){
            close(client_sd);
            snprintf(response, MAX_BUFFER, "Error: Failed to send command to server");
            write(main_clinet_sd, response, strlen(response));
            return ERROR_NETWORK;
        }
        //Read initial response from server
        int responseLen = read(client_sd, response, MAX_BUFFER - 1);
        //If there is error in reading response from server
        if(responseLen <= 0){
            close(client_sd);
            snprintf(response, MAX_BUFFER, "Error: Failed to read response from server");
            write(main_clinet_sd, response, strlen(response));
            return ERROR_NETWORK;
        }
        response[responseLen] = '\0';
        //If there response contains error message from server
        if(strstr(response, "Error:") != NULL || strstr(response, "File does not exist") != NULL){
            //Print the server error message
            close(client_sd);
            write(main_clinet_sd, response, strlen(response));
            return ERROR_NETWORK;
        }
        //Read file size from server(network bytes)
        uint32_t networkFileSize;
        int bytes = read(client_sd, &networkFileSize, sizeof(uint32_t));
        //Use ntohl to convert network bytes to host bytes
        int fileSize = ntohl(networkFileSize);
        //Error if file size is invalid close connection with server and send error message to client
        if(bytes <= 0 || fileSize <= 0 || fileSize > MAX_FILE_SIZE){
            close(client_sd);
            snprintf(response, MAX_BUFFER, "Error: Invalid file size");
            write(main_clinet_sd, response, strlen(response));
            return ERROR_NETWORK;
        }
        //Allocate memory for file based on size
        char *fileData = malloc(fileSize + 1);
        //Error if memory allocation fails
        if(!fileData){
            close(client_sd);
            snprintf(response, MAX_BUFFER, "Error: Memory allocation failed");
            write(main_clinet_sd, response, strlen(response));
            return ERROR_NETWORK;
        }
        //Receive file data in portion from server
        int totalReceived = receiveDataInChunks(client_sd, fileData, fileSize);
        //Error if entire file is not received
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
        //Sleep for 10ms
        usleep(10000);
        //Send file name first to client
        char* lastSlash = strrchr(filePath, '/');
        char* fileName = (lastSlash == NULL) ? filePath : lastSlash + 1;
        if(write(main_clinet_sd, fileName, strlen(fileName)) <= 0){
            free(fileData);
            return ERROR_NETWORK;
        }
        usleep(10000);
        //Send file size using write to client
        //Use htonl to convert host bytes to network bytes
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
        //Free file buffer
        free(fileData);
        //Close the connection with server
        close(client_sd);
    }
    //Return sucess
    return SUCCESS;
}

//Helper function to get file extension
char* getFileExtension(char* filename){
    //Get the last occurenece of '.'
    char* dot = strrchr(filename, '.');
    if(!dot || dot == filename){
        return "";
    }
    //Return the position of '.'
    return dot;
}

//Helper function to verify the existence of file 
int validateFileExist(char *filename) {
    struct stat st;
    //Return 0, if file does not exist
    if (stat(filename, &st) != 0) {
        return 0;
    }
    //Return 1 if file not exist
    return 1;
}

//Helper function to create directory on server
int createDirectory(char* path){
    //Copy path to tempPath
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
    //Check the path contains /S1/
    if(strstr(tempPath, "/S1/")){
        //Skip "/S1"
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

//Helper function to spilt the command
int tokenizeCommand(char* input, char* commandArgs[], int* count){
    //Copy input to copyInput
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
        //Increment the count
        (*count)++;
        portion = strtok(NULL, delimiter);
    }
    return 1;
}

//Function to handle uploadf command
void handleUploadf(int con_sd, char* commandArgs[], int* count){
    //Copy the path from command
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s", commandArgs[*count - 1]);
    char destPath[MAX_PATH];
    strcpy(destPath, path);
    //Replace ~S1 with /home/user/S1
    if(strncmp(destPath, "~S1", 3) == 0){
        //Get the user name
        char* home = getenv("HOME");
        char temp[MAX_PATH];
        //Build the final destination path, skip the first three character(~S1)
        sprintf(temp, "%s/S1%s", home, destPath + 3);
        strcpy(destPath, temp);
    }
    //Create the directory for the dest path
    if(createDirectory(destPath) == -1){
        char* errorMsg = "\nError: Failed to create directory on server.\n";
        write(con_sd, errorMsg, strlen(errorMsg));
        return;
    }
    //Store information about all files in struct
    typedef struct{
        char filename[MAX_PATH];
        char filepath[MAX_PATH];
        char extension[32];
        char* fileBuffer;
        int fileSize;
    } FileInfo;
    //Create array of structure
    FileInfo files[3];
    int numFiles = *count - 2;
    //Process each file
    for(int i = 0; i < numFiles; i++){
        //Copy file name from command
        strcpy(files[i].filename, commandArgs[i + 1]);
        snprintf(files[i].filepath, sizeof(files[i].filepath), "%s/%s", destPath, files[i].filename);
        //Get and copy the file extension
        strcpy(files[i].extension, getFileExtension(files[i].filename));
        //Read file size first using write
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
        //Validate file size
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
        //Error if entire file is not read/received
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
        //Use open to create the file
        int fd = open(files[i].filepath, O_CREAT | O_WRONLY, 0777);
        //Error if open fails
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
        //Write the file on server1
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
    //Now process each file based on extension
    for(int i = 0; i < numFiles; i++){
        char response[MAX_BUFFER];
        int result = SUCCESS;
        //If the file is '.c'
        if(strcmp(files[i].extension, ".c") == 0){
            //Send success as file is already on server 1
            snprintf(response, sizeof(response), "File uploaded successfully to Server");
        }
        //If the file is '.pdf'
        else if(strcmp(files[i].extension, ".pdf") == 0){
            //Copy the filepath to modifiedPath
            char modifiedPath[MAX_PATH];
            strcpy(modifiedPath, files[i].filepath);
            char* s1_ptr = strstr(modifiedPath, "/S1/");
            //Change S1 to S2
            if(s1_ptr != NULL) {
                s1_ptr[2] = '2';  
            }
            //Send the command, path, fileInfo to server2 using communicateWithServer
            result = communicateWithServer("uploadf", modifiedPath, files[i].fileBuffer, files[i].fileSize, server2_ip, server2_port, response, 0);
            //Remove file from Server1 if successfully sent to Server2
            if(result == SUCCESS) {
                unlink(files[i].filepath);
            }
        }
        //If the file is '.txt'
        else if(strcmp(files[i].extension, ".txt") == 0){
            //Copy the filepath to modifiedPath
            char modifiedPath[MAX_PATH];
            strcpy(modifiedPath, files[i].filepath);
            char* s1_ptr = strstr(modifiedPath, "/S1/");
            //Change S1 to S3
            if(s1_ptr != NULL) {
                s1_ptr[2] = '3';  
            }
            //Send the command, path, fileInfo to server3 using communicateWithServer
            result = communicateWithServer("uploadf", modifiedPath, files[i].fileBuffer, files[i].fileSize, server3_ip, server3_port, response, 0);
            // Remove file from Server1 if successfully sent to Server3
            if(result == SUCCESS) {
                unlink(files[i].filepath);
            }
        }
        //If the file is '.zip'
        else if(strcmp(files[i].extension, ".zip") == 0){
            //Copy the filepath to modifiedPath
            char modifiedPath[MAX_PATH];
            strcpy(modifiedPath, files[i].filepath);
            char* s1_ptr = strstr(modifiedPath, "/S1/");
            //Change S1 to S4
            if(s1_ptr != NULL) {
                s1_ptr[2] = '4';
            }
            //Send the command, path, fileInfo to server3 using communicateWithServer
            result = communicateWithServer("uploadf", modifiedPath, files[i].fileBuffer, files[i].fileSize, server4_ip, server4_port, response, 0);
            // Remove file from Server1 if successfully sent to Server4
            if(result == SUCCESS) {
                unlink(files[i].filepath);
            }
        }
        //Write the response to the client
        write(con_sd, response, strlen(response));
        //Free the file buffer
        free(files[i].fileBuffer);
    }   
    //Remove the folder from server1
    rmdir(destPath); 
}

//Function to handle downlf command
void handleDownlf(int con_sd, char* commandArgs[], int* count){
    //Loop through each path
    for(int i = 1; i < *count; i++){
        char response[MAX_BUFFER];
        char ext[32];
        //Copy the file extension to ext
        snprintf(ext, sizeof(ext), "%s",  getFileExtension(commandArgs[i]));
        //If extension is '.c'
        if(strcmp(ext, ".c") == 0){
            char path[MAX_PATH];
            snprintf(path, sizeof(path), "%s", commandArgs[i]);
            char destPath[MAX_PATH];
            strcpy(destPath, path);
            //Prepare the destPath
            //Replace ~S1 with /home/user/S1
            if(strncmp(destPath, "~S1", 3) == 0){
                char* home = getenv("HOME");
                char temp[MAX_PATH];
                sprintf(temp, "%s/S1%s", home, destPath + 3);
                strcpy(destPath, temp);
            }
            //Check the file exist on the dest path
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
            //Open file
            int fd = open(destPath, O_RDONLY);
            if(fd < 0){
                free(fileBuffer);
                snprintf(response, sizeof(response), "Error: Failed to open file on server");
                write(con_sd, response, strlen(response));
                continue;
            }
            //Read file data locally
            int bytesRead = read(fd, fileBuffer, fileSize);
            //Close file
            close(fd);
            //Error if entire file is not read
            if(bytesRead != fileSize){
                free(fileBuffer);
                snprintf(response, sizeof(response), "Error: Failed to read file on server");
                write(con_sd, response, strlen(response));
                continue;
            }
            //Send success read to client first
            snprintf(response, sizeof(response), "Success: File found and ready to transfer");
            if(write(con_sd, response, strlen(response)) <= 0){
                free(fileBuffer);
                continue;
            }
            //Sleep for 10ms
            usleep(10000);
            //Send file name to client using write
            char* lastSlash = strrchr(destPath, '/');
            char* fileName = (lastSlash == NULL) ? destPath : lastSlash + 1;
            if(write(con_sd, fileName, strlen(fileName)) <= 0){
                free(fileBuffer);
                continue;
            }
            usleep(10000);
            //Use htonl to convert host bytes to network bytes
            uint32_t networkFileSizeClient = htonl((uint32_t)fileSize);
            //Send file size to server using write
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
            //Free the file buffer
            free(fileBuffer);
        }
        //If extension is '.pdf'
        else if(strcmp(ext, ".pdf") == 0){
            char path[MAX_PATH];
            snprintf(path, sizeof(path), "%s", commandArgs[i]);
            char destPath[MAX_PATH];
            strcpy(destPath, path);
            //Prepare dest path
            //Replace ~S1 with /home/user/S1
            if(strncmp(destPath, "~S1", 3) == 0){
                char* home = getenv("HOME");
                char temp[MAX_PATH];
                sprintf(temp, "%s/S1%s", home, destPath + 3);
                strcpy(destPath, temp);
            }
            char* s1_ptr = strstr(destPath, "/S1/");
            if(s1_ptr != NULL) {
                //Replace S1 to S2
                s1_ptr[2] = '2';
            }
            struct stat st;
            stat(destPath, &st);
            int fileSize = st.st_size;
            //Send the command, path, fileInfo to server2 using communicateWithServer
            int result = communicateWithServer("downlf", destPath, NULL, fileSize, server2_ip, server2_port, response, con_sd);
            if(result != SUCCESS){
                continue;
            }
        }
        //If extension is '.txt'
        else if(strcmp(ext, ".txt") == 0){
            char path[MAX_PATH];
            snprintf(path, sizeof(path), "%s", commandArgs[i]);
            char destPath[MAX_PATH];
            strcpy(destPath, path);
            //Prepare dest path
            //Replace ~S1 with /home/user/S1
            if(strncmp(destPath, "~S1", 3) == 0){
                char* home = getenv("HOME");
                char temp[MAX_PATH];
                sprintf(temp, "%s/S1%s", home, destPath + 3);
                strcpy(destPath, temp);
            }
            char* s1_ptr = strstr(destPath, "/S1/");
            if(s1_ptr != NULL) {
                //Replace S1 to S3
                s1_ptr[2] = '3';
            }
            struct stat st;
            stat(destPath, &st);
            int fileSize = st.st_size;
            //Send the command, path, fileInfo to server3 using communicateWithServer
            int result = communicateWithServer("downlf", destPath, NULL, fileSize, server3_ip, server3_port, response, con_sd);
            if(result != SUCCESS){
                continue;
            }
        }
        else{
            snprintf(response, sizeof(response), "Error: Invalid extension");
            write(con_sd, response, strlen(response));
        }
    }
}

//Function to handle removef command
void handleRemovef(int con_sd, char* commandArgs[], int* count){
    //Loop through each path
    for(int i = 1; i < *count; i++){
        char response[MAX_BUFFER];
        char ext[32];
        //Copy the extension to ext
        snprintf(ext, sizeof(ext), "%s",  getFileExtension(commandArgs[i]));
        //If extension is '.c'
        if(strcmp(ext, ".c") == 0){
            char path[MAX_PATH];
            snprintf(path, sizeof(path), "%s", commandArgs[i]);
            char destPath[MAX_PATH];
            strcpy(destPath, path);
            //Prepare dest path
            //Replace ~S1 with /home/user/S1
            if(strncmp(destPath, "~S1", 3) == 0){
                char* home = getenv("HOME");
                char temp[MAX_PATH];
                sprintf(temp, "%s/S1%s", home, destPath + 3);
                strcpy(destPath, temp);
            }
            //Validate file exist on server 1
            if(!validateFileExist(destPath)){
                snprintf(response, sizeof(response), "File does not exist on Server");
                write(con_sd, response, strlen(response));
                continue;
            }
            //Remove the file using unlink
            unlink(destPath);
            //Send respond to client
            snprintf(response, sizeof(response), "File removed successfully from Server");
            write(con_sd, response, strlen(response));
        }
        //If extension is '.pdf'
        else if(strcmp(ext, ".pdf") == 0){
            char path[MAX_PATH];
            snprintf(path, sizeof(path), "%s", commandArgs[i]);
            char destPath[MAX_PATH];
            strcpy(destPath, path);
            //Prepare dest path
            //Replace ~S1 with /home/user/S1
            if(strncmp(destPath, "~S1", 3) == 0){
                char* home = getenv("HOME");
                char temp[MAX_PATH];
                sprintf(temp, "%s/S1%s", home, destPath + 3);
                strcpy(destPath, temp);
            }
            char* s1_ptr = strstr(destPath, "/S1/");
            if(s1_ptr != NULL) {
                //Replace S1 to S2
                s1_ptr[2] = '2';
            }
            //Send the command, path, fileInfo to server2 using communicateWithServer
            int result = communicateWithServer("removef", destPath, NULL, 0, server2_ip, server2_port, response, 0);
            write(con_sd, response, strlen(response));
            if(result != SUCCESS){
                continue;
            }
        }
        //If extension is '.txt'
        else if(strcmp(ext, ".txt") == 0){
            char path[MAX_PATH];
            snprintf(path, sizeof(path), "%s", commandArgs[i]);
            char destPath[MAX_PATH];
            strcpy(destPath, path);
            //Prepare dest path
            //Replace ~S1 with /home/user/S1
            if(strncmp(destPath, "~S1", 3) == 0){
                char* home = getenv("HOME");
                char temp[MAX_PATH];
                sprintf(temp, "%s/S1%s", home, destPath + 3);
                strcpy(destPath, temp);
            }
            char* s1_ptr = strstr(destPath, "/S1/");
            if(s1_ptr != NULL) {
                //Replace S1 to S3
                s1_ptr[2] = '3';
            }
            //Send the command, path, fileInfo to server3 using communicateWithServer
            int result = communicateWithServer("removef", destPath, NULL, 0, server3_ip, server3_port, response, 0);
            write(con_sd, response, strlen(response));
            if(result != SUCCESS){
                continue;
            }
        }
        //If extension is '.zip'
        else if(strcmp(ext, ".zip") == 0){
            char path[MAX_PATH];
            snprintf(path, sizeof(path), "%s", commandArgs[i]);
            char destPath[MAX_PATH];
            strcpy(destPath, path);
            //Prepare dest path
            //Replace ~S1 with /home/user/S1
            if(strncmp(destPath, "~S1", 3) == 0){
                char* home = getenv("HOME");
                char temp[MAX_PATH];
                sprintf(temp, "%s/S1%s", home, destPath + 3);
                strcpy(destPath, temp);
            }
            char* s1_ptr = strstr(destPath, "/S1/");
            if(s1_ptr != NULL) {
                //Replace S1 to S4
                s1_ptr[2] = '4';
            }
            //Send the command, path, fileInfo to server4 using communicateWithServer
            int result = communicateWithServer("removef", destPath, NULL, 0, server4_ip, server4_port, response, 0);
            write(con_sd, response, strlen(response));
            if(result != SUCCESS){
                continue;
            }
        }
        else{
            snprintf(response, sizeof(response), "Error: Invalid extension");
            write(con_sd, response, strlen(response));
            continue;
        }
    }
}

//Function to handle downltar command
void handleDownltar(int con_sd, char* commandArgs[], int* count){

}

//Function to handle dispfnames command
void handleDispfnames(int con_sd, char* commandArgs[], int* count){

}

//Function to handle client
void prcclient(int con_sd){
    //Define command and commandArgs to tokenize user input
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
        //Tokenize user entered command
        if(!tokenizeCommand(command, commandArgs, &count)){
            char* errorMsg = "Error: Command tokenization failed.\n";
            write(con_sd, errorMsg, strlen(errorMsg));
            break;
        }
        //If command is uploadf
        if(strcmp(commandArgs[0], "uploadf") == 0){
            //Handle uploadf command
            handleUploadf(con_sd, commandArgs, &count);
        } 
        //If command is downlf
        else if(strcmp(commandArgs[0], "downlf") == 0){
            //Handle downlf command
            handleDownlf(con_sd, commandArgs, &count);
        } 
        //If command is removef
        else if(strcmp(commandArgs[0], "removef") == 0){
            //Handle removef command
            handleRemovef(con_sd, commandArgs, &count);
        } 
        //If command is downltar
        else if(strcmp(commandArgs[0], "downltar") == 0){
            //Handle downltar command
            handleDownltar(con_sd, commandArgs, &count);
        } 
        //If command is dispfnames
        else if(strcmp(commandArgs[0], "dispfnames") == 0){
            //Handle dispfnames command
            handleDispfnames(con_sd, commandArgs, &count);
        }
        //Free the commandArgs array 
        for (int i = 0; i < count; i++) {
            if (commandArgs[i]) {
                free(commandArgs[i]);
                commandArgs[i] = NULL;
            }
        }
    }
    //Final clean-up
    for (int i = 0; i < MAX_COMMAND_ARGS; i++) {
        if (commandArgs[i]) {
            free(commandArgs[i]);
        }
    }
    //Close the connection
    close(con_sd);
}

//Main function
int main(int argc, char *argv[]){
    //Socket variable
    int lis_sd, con_sd, portNumber;
    socklen_t len;
    struct sockaddr_in servAdd;
    int pid;
    //Error if file not run correctly
    if(argc != 8){
        fprintf(stderr, "Usage: %s <Server1_Port> <Server2_IP> <Server2_Port> <Server3_IP> <Server3_Port> <Server4_IP> <Server4_Port>\n", argv[0]);
        exit(0);
    }
    //Assign server 2-4 values to respected variables
    server2_ip = argv[2];
    sscanf(argv[3], "%d", &server2_port);
    server3_ip = argv[4];
    sscanf(argv[5], "%d", &server3_port);
    server4_ip = argv[6];
    sscanf(argv[7], "%d", &server4_port);

    //socket() call
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

    //bind() call
    bind(lis_sd, (struct sockaddr *) &servAdd,sizeof(servAdd));
    //listen max 5 client
    listen(lis_sd, 5);

    while(1){
        //Accept client connection
        con_sd=accept(lis_sd,(struct sockaddr*)NULL,NULL);
        //Fork for client
        pid = fork();
        //Child process service client request using prcclient
        if(pid == 0){
            //Close the listing socket, as not needed
            close(lis_sd);
            prcclient(con_sd);
            //Child terminate when client is done executing command
            exit(0);
        }
        //Parent continues to accept new connection
        else if(pid > 0){
            //Close the connection socket, as not needed
            close(con_sd);
        }
        //Error if fork fails
        else{
            perror("\nFork Failed.\n");
        }
    }
    //Close the listing socket
    close(lis_sd);
    return 0;
}
