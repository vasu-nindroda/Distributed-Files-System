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
#include <dirent.h>

// Global constant
#define MAX_BUFFER 2048
#define MAX_PATH 512
#define MAX_COMMAND_ARGS 5
#define CHUNK_SIZE 8192
#define MAX_FILE_SIZE (50 * 1024 * 1024)

// Response codes
#define SUCCESS 0
#define ERROR_NETWORK -2

// Global variable for server 2-4 communication
char *server2_ip;
int server2_port;
char *server3_ip;
int server3_port;
char *server4_ip;
int server4_port;

// top-level alphabetical comparator for qsort
static int cmpstr(const void *a, const void *b)
{
    const char *sa = *(const char *const *)a;
    const char *sb = *(const char *const *)b;
    return strcmp(sa, sb);
}

// Helper function to send data in parts
int sendDataInChunks(int socket, const char *data, int dataSize)
{
    int totalSent = 0;
    int bytesToSend;
    // Write to socket till all data is not sent
    while (totalSent < dataSize)
    {
        bytesToSend = (dataSize - totalSent > CHUNK_SIZE) ? CHUNK_SIZE : (dataSize - totalSent);
        int sent = write(socket, data + totalSent, bytesToSend);
        // Return -1, if write to socket fails
        if (sent <= 0)
        {
            return -1;
        }
        totalSent += sent;
    }
    return totalSent;
}

// Helper function to receive data in parts
int receiveDataInChunks(int socket, char *buffer, int expectedSize)
{
    int totalReceived = 0;
    int bytesToReceive;
    // Read from socket till all data is not fetch
    while (totalReceived < expectedSize)
    {
        bytesToReceive = (expectedSize - totalReceived > CHUNK_SIZE) ? CHUNK_SIZE : (expectedSize - totalReceived);
        int received = read(socket, buffer + totalReceived, bytesToReceive);
        // Return -1, if read from socket fails
        if (received <= 0)
        {
            return -1;
        }
        totalReceived += received;
    }
    return totalReceived;
}

// Function to communicate with other server using server_port and server_ip
int communicateWithServer(char *commandType, char *filePath, char *fileBuffer, int fileSize, char *sIp, int sPort, char *response, int main_clinet_sd)
{
    // Socket variable
    int client_sd;
    struct sockaddr_in server_addr;
    // Socket call
    if ((client_sd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        fprintf(stderr, "Could not create socket\n");
        exit(1);
    }
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(sPort);
    inet_pton(AF_INET, sIp, &server_addr.sin_addr);
    // Connect call to connect with other server
    if (connect(client_sd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        close(client_sd);
        return -1;
    }
    // If command is uploadf
    if (strcmp(commandType, "uploadf") == 0)
    {
        // Frist send the command to server using write
        char command[MAX_BUFFER];
        snprintf(command, MAX_BUFFER, "uploadf %s", filePath);
        if (write(client_sd, command, strlen(command)) < 0)
        {
            // If write fails, close connection and send error message
            close(client_sd);
            strcpy(response, "Error: Failed to send command to server");
            return ERROR_NETWORK;
        }
        // Sleep for 10ms
        usleep(10000);
        // Use htonl to convert host bytes to network bytes
        uint32_t networkFileSize = htonl((uint32_t)fileSize);
        // Send file size to server using write
        if (write(client_sd, &networkFileSize, sizeof(networkFileSize)) != sizeof(networkFileSize))
        {
            close(client_sd);
            strcpy(response, "Error: Failed to send file size to Server");
            return ERROR_NETWORK;
        }
        // Send file data in chunk
        if (sendDataInChunks(client_sd, fileBuffer, fileSize) != fileSize)
        {
            // Error if all data is not sent
            close(client_sd);
            strcpy(response, "Error: Failed to send file data to Server");
            return ERROR_NETWORK;
        }
        // Read response from server
        int responseLen = read(client_sd, response, MAX_BUFFER - 1);
        if (responseLen <= 0)
        {
            strcpy(response, "Error: No response from Server");
            close(client_sd);
            return ERROR_NETWORK;
        }
        response[responseLen] = '\0';
        // Close the connection
        close(client_sd);
    }
    // If command is removef
    if (strcmp(commandType, "removef") == 0)
    {
        char command[MAX_BUFFER];
        snprintf(command, MAX_BUFFER, "removef %s", filePath);
        // Frist send the command to server using write
        if (write(client_sd, command, strlen(command)) < 0)
        {
            close(client_sd);
            strcpy(response, "Error: Failed to send command to server");
            return ERROR_NETWORK;
        }
        // Read response from server
        int responseLen = read(client_sd, response, MAX_BUFFER - 1);
        if (responseLen <= 0)
        {
            strcpy(response, "Error: No response from Server");
            close(client_sd);
            return ERROR_NETWORK;
        }
        response[responseLen] = '\0';
        // Close the connection
        close(client_sd);
    }
    // If command is downlf
    if (strcmp(commandType, "downlf") == 0)
    {
        char command[MAX_BUFFER];
        snprintf(command, MAX_BUFFER, "downlf %s", filePath);
        // Frist send the command to server using write
        if (write(client_sd, command, strlen(command)) < 0)
        {
            close(client_sd);
            snprintf(response, MAX_BUFFER, "Error: Failed to send command to server");
            write(main_clinet_sd, response, strlen(response));
            return ERROR_NETWORK;
        }
        // Read initial response from server
        int responseLen = read(client_sd, response, MAX_BUFFER - 1);
        // If there is error in reading response from server
        if (responseLen <= 0)
        {
            close(client_sd);
            snprintf(response, MAX_BUFFER, "Error: Failed to read response from server");
            write(main_clinet_sd, response, strlen(response));
            return ERROR_NETWORK;
        }
        response[responseLen] = '\0';
        // If there response contains error message from server
        if (strstr(response, "Error:") != NULL || strstr(response, "File does not exist") != NULL)
        {
            // Print the server error message
            close(client_sd);
            write(main_clinet_sd, response, strlen(response));
            return ERROR_NETWORK;
        }
        // Read file size from server(network bytes)
        uint32_t networkFileSize;
        int bytes = read(client_sd, &networkFileSize, sizeof(uint32_t));
        // Use ntohl to convert network bytes to host bytes
        int fileSize = ntohl(networkFileSize);
        // Error if file size is invalid close connection with server and send error message to client
        if (bytes <= 0 || fileSize <= 0 || fileSize > MAX_FILE_SIZE)
        {
            close(client_sd);
            snprintf(response, MAX_BUFFER, "Error: Invalid file size");
            write(main_clinet_sd, response, strlen(response));
            return ERROR_NETWORK;
        }
        // Allocate memory for file based on size
        char *fileData = malloc(fileSize + 1);
        // Error if memory allocation fails
        if (!fileData)
        {
            close(client_sd);
            snprintf(response, MAX_BUFFER, "Error: Memory allocation failed");
            write(main_clinet_sd, response, strlen(response));
            return ERROR_NETWORK;
        }
        // Receive file data in portion from server
        int totalReceived = receiveDataInChunks(client_sd, fileData, fileSize);
        // Error if entire file is not received
        if (totalReceived != fileSize)
        {
            free(fileData);
            close(client_sd);
            snprintf(response, MAX_BUFFER, "Error: Failed to receive complete file data");
            write(main_clinet_sd, response, strlen(response));
            return ERROR_NETWORK;
        }
        // Send success response to client first
        snprintf(response, MAX_BUFFER, "Success: File retrieved from target server");
        if (write(main_clinet_sd, response, strlen(response)) <= 0)
        {
            free(fileData);
            return ERROR_NETWORK;
        }
        // Sleep for 10ms
        usleep(10000);
        // Send file name first to client
        char *lastSlash = strrchr(filePath, '/');
        char *fileName = (lastSlash == NULL) ? filePath : lastSlash + 1;
        if (write(main_clinet_sd, fileName, strlen(fileName)) <= 0)
        {
            free(fileData);
            return ERROR_NETWORK;
        }
        usleep(10000);
        // Send file size using write to client
        // Use htonl to convert host bytes to network bytes
        uint32_t networkFileSizeClient = htonl((uint32_t)fileSize);
        if (write(main_clinet_sd, &networkFileSizeClient, sizeof(networkFileSizeClient)) != sizeof(networkFileSizeClient))
        {
            free(fileData);
            return ERROR_NETWORK;
        }
        usleep(10000);
        // Send file data in chunk to client
        if (sendDataInChunks(main_clinet_sd, fileData, fileSize) != fileSize)
        {
            free(fileData);
            return ERROR_NETWORK;
        }
        // Free file buffer
        free(fileData);
        // Close the connection with server
        close(client_sd);
    }
    // Return sucess
    return SUCCESS;
}

// Helper function to get file extension
char *getFileExtension(char *filename)
{
    // Get the last occurenece of '.'
    char *dot = strrchr(filename, '.');
    if (!dot || dot == filename)
    {
        return "";
    }
    // Return the position of '.'
    return dot;
}

// Helper function to verify the existence of file
int validateFileExist(char *filename)
{
    struct stat st;
    // Return 0, if file does not exist
    if (stat(filename, &st) != 0)
    {
        return 0;
    }
    // Return 1 if file not exist
    return 1;
}

// Helper function to create directory on server
int createDirectory(char *path)
{
    // Copy path to tempPath
    char tempPath[MAX_PATH];
    char *p = NULL;
    int len;
    // Copy the input path
    snprintf(tempPath, sizeof(tempPath), "%s", path);
    len = strlen(tempPath);
    // Remove trailing slash
    if (tempPath[len - 1] == '/')
    {
        tempPath[len - 1] = 0;
    }
    char *baseEnd = NULL;
    // Check the path contains /S1/
    if (strstr(tempPath, "/S1/"))
    {
        // Skip "/S1"
        baseEnd = strstr(tempPath, "/S1/") + 3;
    }
    // Iterate through the path and create subdirectories
    for (p = baseEnd; *p; p++)
    {
        if (*p == '/')
        {
            *p = 0;
            int result = mkdir(tempPath, 0755);
            if (result == -1 && errno != EEXIST)
            {
                return -1;
            }
            *p = '/';
        }
    }
    int result = mkdir(tempPath, 0755);
    if (result == -1 && errno != EEXIST)
    {
        return -1;
    }
    return 0;
}

// Helper function to spilt the command
int tokenizeCommand(char *input, char *commandArgs[], int *count)
{
    // Copy input to copyInput
    char copyInput[MAX_BUFFER];
    strcpy(copyInput, input);
    char *delimiter = " \t";
    // Split on the base of delimiter using strtok
    char *portion = strtok(copyInput, delimiter);
    while (portion != NULL)
    {
        commandArgs[*count] = malloc(strlen(portion) + 1);
        if (commandArgs[*count] == NULL)
        {
            printf("\nError: Memory allocation failed.\n");
            return 0;
        }
        // Copy the split portion to commandArgs[]
        strcpy(commandArgs[*count], portion);
        // Increment the count
        (*count)++;
        portion = strtok(NULL, delimiter);
    }
    return 1;
}

// Function to handle uploadf command
void handleUploadf(int con_sd, char *commandArgs[], int *count)
{
    // Copy the path from command
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s", commandArgs[*count - 1]);
    char destPath[MAX_PATH];
    strcpy(destPath, path);
    // Replace ~S1 with /home/user/S1
    if (strncmp(destPath, "~S1", 3) == 0)
    {
        // Get the user name
        char *home = getenv("HOME");
        char temp[MAX_PATH];
        // Build the final destination path, skip the first three character(~S1)
        sprintf(temp, "%s/S1%s", home, destPath + 3);
        strcpy(destPath, temp);
    }
    // Create the directory for the dest path
    if (createDirectory(destPath) == -1)
    {
        char *errorMsg = "\nError: Failed to create directory on server.\n";
        write(con_sd, errorMsg, strlen(errorMsg));
        return;
    }
    // Store information about all files in struct
    typedef struct
    {
        char filename[MAX_PATH];
        char filepath[MAX_PATH];
        char extension[32];
        char *fileBuffer;
        int fileSize;
    } FileInfo;
    // Create array of structure
    FileInfo files[3];
    int numFiles = *count - 2;
    // Process each file
    for (int i = 0; i < numFiles; i++)
    {
        // Copy file name from command
        strcpy(files[i].filename, commandArgs[i + 1]);
        snprintf(files[i].filepath, sizeof(files[i].filepath), "%s/%s", destPath, files[i].filename);
        // Get and copy the file extension
        strcpy(files[i].extension, getFileExtension(files[i].filename));
        // Read file size first using write
        if (read(con_sd, &files[i].fileSize, sizeof(int)) <= 0)
        {
            char *errorMsg = "\nError: Failed to receive file size.\n";
            write(con_sd, errorMsg, strlen(errorMsg));
            for (int j = 0; j < i; j++)
            {
                if (files[j].fileBuffer)
                {
                    free(files[j].fileBuffer);
                }
            }
            return;
        }
        // Validate file size
        if (files[i].fileSize <= 0 || files[i].fileSize > MAX_FILE_SIZE)
        {
            char *errorMsg = "\nError: Invalid file size.\n";
            write(con_sd, errorMsg, strlen(errorMsg));
            for (int j = 0; j < i; j++)
            {
                if (files[j].fileBuffer)
                {
                    free(files[j].fileBuffer);
                }
            }
            return;
        }
        // Allocate buffer for file data
        files[i].fileBuffer = malloc(files[i].fileSize + 1);
        if (!files[i].fileBuffer)
        {
            char *errorMsg = "\nError: Memory allocation failed.\n";
            write(con_sd, errorMsg, strlen(errorMsg));
            for (int j = 0; j < i; j++)
            {
                if (files[j].fileBuffer)
                {
                    free(files[j].fileBuffer);
                }
            }
            return;
        }
        // Receive file data in chunks
        int bytesReceived = receiveDataInChunks(con_sd, files[i].fileBuffer, files[i].fileSize);
        // Error if entire file is not read/received
        if (bytesReceived != files[i].fileSize)
        {
            char *errorMsg = "\nError: Failed to receive complete file data.\n";
            write(con_sd, errorMsg, strlen(errorMsg));
            for (int j = 0; j < i; j++)
            {
                if (files[j].fileBuffer)
                {
                    free(files[j].fileBuffer);
                }
            }
            return;
        }
        // Store file on Server1 first
        // Use open to create the file
        int fd = open(files[i].filepath, O_CREAT | O_WRONLY, 0777);
        // Error if open fails
        if (fd < 0)
        {
            char *errorMsg = "\nError: Failed to create file on Server1.\n";
            write(con_sd, errorMsg, strlen(errorMsg));
            for (int j = 0; j <= i; j++)
            {
                if (files[j].fileBuffer)
                {
                    free(files[j].fileBuffer);
                }
            }
            return;
        }
        // Write the file on server1
        if (write(fd, files[i].fileBuffer, files[i].fileSize) != files[i].fileSize)
        {
            close(fd);
            char *errorMsg = "\nError: Failed to write file on Server1.\n";
            write(con_sd, errorMsg, strlen(errorMsg));
            // Clean up all allocated buffers
            for (int j = 0; j <= i; j++)
            {
                if (files[j].fileBuffer)
                {
                    free(files[j].fileBuffer);
                }
            }
            return;
        }
        close(fd);
    }
    // Now process each file based on extension
    for (int i = 0; i < numFiles; i++)
    {
        char response[MAX_BUFFER];
        int result = SUCCESS;
        // If the file is '.c'
        if (strcmp(files[i].extension, ".c") == 0)
        {
            // Send success as file is already on server 1
            snprintf(response, sizeof(response), "File uploaded successfully to Server");
        }
        // If the file is '.pdf'
        else if (strcmp(files[i].extension, ".pdf") == 0)
        {
            // Copy the filepath to modifiedPath
            char modifiedPath[MAX_PATH];
            strcpy(modifiedPath, files[i].filepath);
            char *s1_ptr = strstr(modifiedPath, "/S1/");
            // Change S1 to S2
            if (s1_ptr != NULL)
            {
                s1_ptr[2] = '2';
            }
            // Send the command, path, fileInfo to server2 using communicateWithServer
            result = communicateWithServer("uploadf", modifiedPath, files[i].fileBuffer, files[i].fileSize, server2_ip, server2_port, response, 0);
            // Remove file from Server1 if successfully sent to Server2
            if (result == SUCCESS)
            {
                unlink(files[i].filepath);
            }
        }
        // If the file is '.txt'
        else if (strcmp(files[i].extension, ".txt") == 0)
        {
            // Copy the filepath to modifiedPath
            char modifiedPath[MAX_PATH];
            strcpy(modifiedPath, files[i].filepath);
            char *s1_ptr = strstr(modifiedPath, "/S1/");
            // Change S1 to S3
            if (s1_ptr != NULL)
            {
                s1_ptr[2] = '3';
            }
            // Send the command, path, fileInfo to server3 using communicateWithServer
            result = communicateWithServer("uploadf", modifiedPath, files[i].fileBuffer, files[i].fileSize, server3_ip, server3_port, response, 0);
            // Remove file from Server1 if successfully sent to Server3
            if (result == SUCCESS)
            {
                unlink(files[i].filepath);
            }
        }
        // If the file is '.zip'
        else if (strcmp(files[i].extension, ".zip") == 0)
        {
            // Copy the filepath to modifiedPath
            char modifiedPath[MAX_PATH];
            strcpy(modifiedPath, files[i].filepath);
            char *s1_ptr = strstr(modifiedPath, "/S1/");
            // Change S1 to S4
            if (s1_ptr != NULL)
            {
                s1_ptr[2] = '4';
            }
            // Send the command, path, fileInfo to server3 using communicateWithServer
            result = communicateWithServer("uploadf", modifiedPath, files[i].fileBuffer, files[i].fileSize, server4_ip, server4_port, response, 0);
            // Remove file from Server1 if successfully sent to Server4
            if (result == SUCCESS)
            {
                unlink(files[i].filepath);
            }
        }
        // Write the response to the client
        write(con_sd, response, strlen(response));
        // Free the file buffer
        free(files[i].fileBuffer);
    }
    // Remove the folder from server1
    rmdir(destPath);
}

// Function to handle downlf command
void handleDownlf(int con_sd, char *commandArgs[], int *count)
{
    // Loop through each path
    for (int i = 1; i < *count; i++)
    {
        char response[MAX_BUFFER];
        char ext[32];
        // Copy the file extension to ext
        snprintf(ext, sizeof(ext), "%s", getFileExtension(commandArgs[i]));
        // If extension is '.c'
        if (strcmp(ext, ".c") == 0)
        {
            char path[MAX_PATH];
            snprintf(path, sizeof(path), "%s", commandArgs[i]);
            char destPath[MAX_PATH];
            strcpy(destPath, path);
            // Prepare the destPath
            // Replace ~S1 with /home/user/S1
            if (strncmp(destPath, "~S1", 3) == 0)
            {
                char *home = getenv("HOME");
                char temp[MAX_PATH];
                sprintf(temp, "%s/S1%s", home, destPath + 3);
                strcpy(destPath, temp);
            }
            // Check the file exist on the dest path
            if (!validateFileExist(destPath))
            {
                snprintf(response, sizeof(response), "Error: File does not exist on Server");
                write(con_sd, response, strlen(response));
                continue;
            }
            // Get file size
            struct stat st;
            stat(destPath, &st);
            int fileSize = st.st_size;
            char *fileBuffer = malloc(fileSize + 1);
            if (!fileBuffer)
            {
                snprintf(response, sizeof(response), "Error: Memory allocation failed");
                write(con_sd, response, strlen(response));
                continue;
            }
            // Open file
            int fd = open(destPath, O_RDONLY);
            if (fd < 0)
            {
                free(fileBuffer);
                snprintf(response, sizeof(response), "Error: Failed to open file on server");
                write(con_sd, response, strlen(response));
                continue;
            }
            // Read file data locally
            int bytesRead = read(fd, fileBuffer, fileSize);
            // Close file
            close(fd);
            // Error if entire file is not read
            if (bytesRead != fileSize)
            {
                free(fileBuffer);
                snprintf(response, sizeof(response), "Error: Failed to read file on server");
                write(con_sd, response, strlen(response));
                continue;
            }
            // Send success read to client first
            snprintf(response, sizeof(response), "Success: File found and ready to transfer");
            if (write(con_sd, response, strlen(response)) <= 0)
            {
                free(fileBuffer);
                continue;
            }
            // Sleep for 10ms
            usleep(10000);
            // Send file name to client using write
            char *lastSlash = strrchr(destPath, '/');
            char *fileName = (lastSlash == NULL) ? destPath : lastSlash + 1;
            if (write(con_sd, fileName, strlen(fileName)) <= 0)
            {
                free(fileBuffer);
                continue;
            }
            usleep(10000);
            // Use htonl to convert host bytes to network bytes
            uint32_t networkFileSizeClient = htonl((uint32_t)fileSize);
            // Send file size to server using write
            if (write(con_sd, &networkFileSizeClient, sizeof(networkFileSizeClient)) != sizeof(networkFileSizeClient))
            {
                strcpy(response, "Error: Failed to send file size");
                free(fileBuffer);
                continue;
            }
            usleep(10000);
            // Send file data in chunk to client
            if (sendDataInChunks(con_sd, fileBuffer, fileSize) != fileSize)
            {
                strcpy(response, "Error: Failed to send file data to clinet");
                free(fileBuffer);
                continue;
            }
            // Free the file buffer
            free(fileBuffer);
        }
        // If extension is '.pdf'
        else if (strcmp(ext, ".pdf") == 0)
        {
            char path[MAX_PATH];
            snprintf(path, sizeof(path), "%s", commandArgs[i]);
            char destPath[MAX_PATH];
            strcpy(destPath, path);
            // Prepare dest path
            // Replace ~S1 with /home/user/S1
            if (strncmp(destPath, "~S1", 3) == 0)
            {
                char *home = getenv("HOME");
                char temp[MAX_PATH];
                sprintf(temp, "%s/S1%s", home, destPath + 3);
                strcpy(destPath, temp);
            }
            char *s1_ptr = strstr(destPath, "/S1/");
            if (s1_ptr != NULL)
            {
                // Replace S1 to S2
                s1_ptr[2] = '2';
            }
            struct stat st;
            stat(destPath, &st);
            int fileSize = st.st_size;
            // Send the command, path, fileInfo to server2 using communicateWithServer
            int result = communicateWithServer("downlf", destPath, NULL, fileSize, server2_ip, server2_port, response, con_sd);
            if (result != SUCCESS)
            {
                continue;
            }
        }
        // If extension is '.txt'
        else if (strcmp(ext, ".txt") == 0)
        {
            char path[MAX_PATH];
            snprintf(path, sizeof(path), "%s", commandArgs[i]);
            char destPath[MAX_PATH];
            strcpy(destPath, path);
            // Prepare dest path
            // Replace ~S1 with /home/user/S1
            if (strncmp(destPath, "~S1", 3) == 0)
            {
                char *home = getenv("HOME");
                char temp[MAX_PATH];
                sprintf(temp, "%s/S1%s", home, destPath + 3);
                strcpy(destPath, temp);
            }
            char *s1_ptr = strstr(destPath, "/S1/");
            if (s1_ptr != NULL)
            {
                // Replace S1 to S3
                s1_ptr[2] = '3';
            }
            struct stat st;
            stat(destPath, &st);
            int fileSize = st.st_size;
            // Send the command, path, fileInfo to server3 using communicateWithServer
            int result = communicateWithServer("downlf", destPath, NULL, fileSize, server3_ip, server3_port, response, con_sd);
            if (result != SUCCESS)
            {
                continue;
            }
        }
        else
        {
            snprintf(response, sizeof(response), "Error: Invalid extension");
            write(con_sd, response, strlen(response));
        }
    }
}

// Function to handle removef command
void handleRemovef(int con_sd, char *commandArgs[], int *count)
{
    // Loop through each path
    for (int i = 1; i < *count; i++)
    {
        char response[MAX_BUFFER];
        char ext[32];
        // Copy the extension to ext
        snprintf(ext, sizeof(ext), "%s", getFileExtension(commandArgs[i]));
        // If extension is '.c'
        if (strcmp(ext, ".c") == 0)
        {
            char path[MAX_PATH];
            snprintf(path, sizeof(path), "%s", commandArgs[i]);
            char destPath[MAX_PATH];
            strcpy(destPath, path);
            // Prepare dest path
            // Replace ~S1 with /home/user/S1
            if (strncmp(destPath, "~S1", 3) == 0)
            {
                char *home = getenv("HOME");
                char temp[MAX_PATH];
                sprintf(temp, "%s/S1%s", home, destPath + 3);
                strcpy(destPath, temp);
            }
            // Validate file exist on server 1
            if (!validateFileExist(destPath))
            {
                snprintf(response, sizeof(response), "File does not exist on Server");
                write(con_sd, response, strlen(response));
                continue;
            }
            // Remove the file using unlink
            unlink(destPath);
            // Send respond to client
            snprintf(response, sizeof(response), "File removed successfully from Server");
            write(con_sd, response, strlen(response));
        }
        // If extension is '.pdf'
        else if (strcmp(ext, ".pdf") == 0)
        {
            char path[MAX_PATH];
            snprintf(path, sizeof(path), "%s", commandArgs[i]);
            char destPath[MAX_PATH];
            strcpy(destPath, path);
            // Prepare dest path
            // Replace ~S1 with /home/user/S1
            if (strncmp(destPath, "~S1", 3) == 0)
            {
                char *home = getenv("HOME");
                char temp[MAX_PATH];
                sprintf(temp, "%s/S1%s", home, destPath + 3);
                strcpy(destPath, temp);
            }
            char *s1_ptr = strstr(destPath, "/S1/");
            if (s1_ptr != NULL)
            {
                // Replace S1 to S2
                s1_ptr[2] = '2';
            }
            // Send the command, path, fileInfo to server2 using communicateWithServer
            int result = communicateWithServer("removef", destPath, NULL, 0, server2_ip, server2_port, response, 0);
            write(con_sd, response, strlen(response));
            if (result != SUCCESS)
            {
                continue;
            }
        }
        // If extension is '.txt'
        else if (strcmp(ext, ".txt") == 0)
        {
            char path[MAX_PATH];
            snprintf(path, sizeof(path), "%s", commandArgs[i]);
            char destPath[MAX_PATH];
            strcpy(destPath, path);
            // Prepare dest path
            // Replace ~S1 with /home/user/S1
            if (strncmp(destPath, "~S1", 3) == 0)
            {
                char *home = getenv("HOME");
                char temp[MAX_PATH];
                sprintf(temp, "%s/S1%s", home, destPath + 3);
                strcpy(destPath, temp);
            }
            char *s1_ptr = strstr(destPath, "/S1/");
            if (s1_ptr != NULL)
            {
                // Replace S1 to S3
                s1_ptr[2] = '3';
            }
            // Send the command, path, fileInfo to server3 using communicateWithServer
            int result = communicateWithServer("removef", destPath, NULL, 0, server3_ip, server3_port, response, 0);
            write(con_sd, response, strlen(response));
            if (result != SUCCESS)
            {
                continue;
            }
        }
        // If extension is '.zip'
        else if (strcmp(ext, ".zip") == 0)
        {
            char path[MAX_PATH];
            snprintf(path, sizeof(path), "%s", commandArgs[i]);
            char destPath[MAX_PATH];
            strcpy(destPath, path);
            // Prepare dest path
            // Replace ~S1 with /home/user/S1
            if (strncmp(destPath, "~S1", 3) == 0)
            {
                char *home = getenv("HOME");
                char temp[MAX_PATH];
                sprintf(temp, "%s/S1%s", home, destPath + 3);
                strcpy(destPath, temp);
            }
            char *s1_ptr = strstr(destPath, "/S1/");
            if (s1_ptr != NULL)
            {
                // Replace S1 to S4
                s1_ptr[2] = '4';
            }
            // Send the command, path, fileInfo to server4 using communicateWithServer
            int result = communicateWithServer("removef", destPath, NULL, 0, server4_ip, server4_port, response, 0);
            write(con_sd, response, strlen(response));
            if (result != SUCCESS)
            {
                continue;
            }
        }
        else
        {
            snprintf(response, sizeof(response), "Error: Invalid extension");
            write(con_sd, response, strlen(response));
            continue;
        }
    }
}

// S1: choose server by extension, build locally for .c

static int make_tar_for_ext(const char *baseDir, const char *ext,
                            char *tmpTarPath, size_t tlen,
                            char *outName, size_t nlen)
{
    // spec names
    if (!strcmp(ext, ".c"))
        snprintf(outName, nlen, "cfiles.tar");
    else if (!strcmp(ext, ".pdf"))
        snprintf(outName, nlen, "pdf.tar");
    else if (!strcmp(ext, ".txt"))
        snprintf(outName, nlen, "text.tar");
    else
        return -1;

    snprintf(tmpTarPath, tlen, "/tmp/downltar_%d.tar", (int)getpid());

    char cmd[1024];
    // IMPORTANT: 'cd ... || exit 1' prevents hangs if baseDir is wrong
    snprintf(cmd, sizeof(cmd),
             "sh -c 'cd \"%s\" 2>/dev/null || exit 1; "
             "find . -type f -name \"*%s\" -print0 2>/dev/null | "
             "tar -cf \"%s\" --null -T - 2>/dev/null'",
             baseDir, ext, tmpTarPath);

    int rc = system(cmd);
    if (rc != 0)
    {
        unlink(tmpTarPath);
        return -1;
    }

    struct stat st;
    if (stat(tmpTarPath, &st) != 0)
    {
        unlink(tmpTarPath);
        return -1;
    }
    return 0;
}

// S1: proxy to S2/S3 and forward status/name/size/payload to the client
static int proxy_tar_from_other_server(int client_sd,
                                       const char *server_ip, int server_port,
                                       const char *ext)
{ // Create a socket and connect to the server
    int sd = socket(AF_INET, SOCK_STREAM, 0);
    if (sd < 0)
        return -1;
    // Set socket options to avoid TIME_WAIT issues
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(server_port);
    if (inet_pton(AF_INET, server_ip, &addr.sin_addr) <= 0 ||
        connect(sd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        close(sd);
        return -1;
    }
    // Send the command to the server
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "downltar %s", ext);
    if (write(sd, cmd, strlen(cmd)) <= 0)
    {
        close(sd);
        return -1;
    }

    // 1) status
    char status[MAX_BUFFER];
    int sl = read(sd, status, sizeof(status) - 1);
    if (sl <= 0)
    {
        close(sd);
        return -1;
    }
    status[sl] = '\0';
    if (write(client_sd, status, sl) != sl)
    {
        close(sd);
        return -1;
    }
    if (strstr(status, "Error:"))
    {
        close(sd);
        return 0;
    } // already forwarded

    // 2) name
    char name[256] = {0};
    int nl = read(sd, name, sizeof(name));
    if (nl <= 0)
    {
        close(sd);
        return -1;
    }
    if (write(client_sd, name, nl) != nl)
    {
        close(sd);
        return -1;
    }
    usleep(10000);

    // 3) size
    uint32_t netSz;
    if (read(sd, &netSz, sizeof(netSz)) != sizeof(netSz))
    {
        close(sd);
        return -1;
    }
    if (write(client_sd, &netSz, sizeof(netSz)) != sizeof(netSz))
    {
        close(sd);
        return -1;
    }
    usleep(10000);

    // 4) payload
    int left = ntohl(netSz);
    char buf[CHUNK_SIZE];
    while (left > 0)
    {
        int r = read(sd, buf, (left > CHUNK_SIZE ? CHUNK_SIZE : left));
        if (r <= 0)
        {
            close(sd);
            return -1;
        }
        if (sendDataInChunks(client_sd, buf, r) != r)
        {
            close(sd);
            return -1;
        }
        left -= r;
    }
    close(sd);
    return 0;
}
// Function to handle downltar command
void handleDownltar(int con_sd, char *commandArgs[], int *count)
{
    if (*count != 2)
    {
        const char *msg = "Error: downltar needs one arg: .c/.pdf/.txt";
        write(con_sd, msg, strlen(msg));
        return;
    }

    const char *ext = commandArgs[1];
    char *home = getenv("HOME");
    if (!home)
    {
        write(con_sd, "Error: HOME not set", 19);
        return;
    }

    if (!strcmp(ext, ".c"))
    {
        // local on S1 → $HOME/S1
        char base[MAX_PATH], tarTmp[MAX_PATH], tarName[64];
        snprintf(base, sizeof(base), "%s/S1", home);

        if (make_tar_for_ext(base, ext, tarTmp, sizeof(tarTmp), tarName, sizeof(tarName)) != 0)
        {
            write(con_sd, "Error: Failed to build tar", 27);
            return;
        }

        // status + name
        write(con_sd, "Success: Tar ready", 19);
        usleep(10000);
        write(con_sd, tarName, strlen(tarName));
        usleep(10000);

        // size + stream
        struct stat st;
        stat(tarTmp, &st);
        uint32_t netSz = htonl((uint32_t)st.st_size);
        write(con_sd, &netSz, sizeof(netSz));
        usleep(10000);

        int fd = open(tarTmp, O_RDONLY);
        char buf[CHUNK_SIZE];
        int r;
        while ((r = read(fd, buf, sizeof(buf))) > 0)
        {
            if (sendDataInChunks(con_sd, buf, r) != r)
            {
                close(fd);
                unlink(tarTmp);
                return;
            }
        }
        close(fd);
        unlink(tarTmp);
        return;
    }

    // route .pdf to S2, .txt to S3
    if (!strcmp(ext, ".pdf"))
    {
        if (proxy_tar_from_other_server(con_sd, server2_ip, server2_port, ".pdf") < 0)
            write(con_sd, "Error: Failed to fetch tar from S2", 34);
        return;
    }
    if (!strcmp(ext, ".txt"))
    {
        if (proxy_tar_from_other_server(con_sd, server3_ip, server3_port, ".txt") < 0)
            write(con_sd, "Error: Failed to fetch tar from S3", 34);
        return;
    }

    write(con_sd, "Error: Unsupported extension", 28);
}

// --- S1: dispfnames helpers ---

// collect basenames (non-recursive) that end with ext (e.g., ".c") from dir
static int collect_names_one_dir(const char *dir, const char *ext, char ***outList, int *outCount)
{
    DIR *dp = opendir(dir);
    if (!dp)
    {
        *outList = NULL;
        *outCount = 0;
        return -1;
    } // treat as empty if not accessible

    struct dirent *de;
    int cap = 16, n = 0;
    char **arr = (char **)malloc(cap * sizeof(char *));
    if (!arr)
    {
        closedir(dp);
        return -1;
    }

    while ((de = readdir(dp)) != NULL)
    {
        // skip . and ..
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;

        // must match extension
        const char *dot = strrchr(de->d_name, '.');
        if (!dot || strcmp(dot, ext) != 0)
            continue;

        if (n == cap)
        {
            cap *= 2;
            char **tmp = (char **)realloc(arr, cap * sizeof(char *));
            if (!tmp)
            { // clean up so far
                for (int i = 0; i < n; i++)
                    free(arr[i]);
                free(arr);
                closedir(dp);
                return -1;
            }
            arr = tmp;
        }
        arr[n] = strdup(de->d_name);
        if (!arr[n])
        { // clean up so far
            for (int i = 0; i < n; i++)
                free(arr[i]);
            free(arr);
            closedir(dp);
            return -1;
        }
        n++;
    }
    closedir(dp);

    // sort alphabetically

    if (n > 1)
    {
        qsort(arr, n, sizeof(char *), cmpstr);
    }
    *outList = arr;
    *outCount = n;
    return 0;
}

// join an array of strings into one newline-separated blob
static char *join_names(char **names, int count, int *outLen)
{
    size_t total = 0;
    for (int i = 0; i < count; i++)
        total += strlen(names[i]) + 1; // + '\n'
    if (total == 0)
    {
        *outLen = 0;
        return NULL;
    }

    char *buf = (char *)malloc(total);
    if (!buf)
    {
        *outLen = 0;
        return NULL;
    }

    size_t off = 0;
    for (int i = 0; i < count; i++)
    {
        size_t L = strlen(names[i]);
        memcpy(buf + off, names[i], L);
        off += L;
        buf[off++] = '\n';
    }
    *outLen = (int)off;
    return buf;
}

// ask a peer (S2/S3/S4) to list names from dir for a given ext; returns malloc'ed buffer
static int fetch_names_from_peer(const char *ip, int port,
                                 const char *dirAbs, const char *ext,
                                 char **outBuf, int *outLen)
{
    *outBuf = NULL;
    *outLen = 0;

    int sd = socket(AF_INET, SOCK_STREAM, 0);
    if (sd < 0)
        return -1;

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0)
    {
        close(sd);
        return -1;
    }
    if (connect(sd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        close(sd);
        return -1;
    }

    char line[MAX_PATH + 64];
    snprintf(line, sizeof(line), "dispfnames %s %s", dirAbs, ext);
    if (write(sd, line, strlen(line)) <= 0)
    {
        close(sd);
        return -1;
    }

    // 1) read status
    char status[MAX_BUFFER];
    int sl = read(sd, status, sizeof(status) - 1);
    if (sl <= 0)
    {
        close(sd);
        return -1;
    }
    status[sl] = '\0';
    if (strstr(status, "Error:"))
    {
        close(sd);
        return -2;
    }

    // 2) read size
    uint32_t netSz;
    if (read(sd, &netSz, sizeof(netSz)) != sizeof(netSz))
    {
        close(sd);
        return -1;
    }
    int sz = ntohl(netSz);
    if (sz < 0 || sz > MAX_FILE_SIZE)
    {
        close(sd);
        return -1;
    }

    if (sz == 0)
    {
        close(sd);
        *outBuf = NULL;
        *outLen = 0;
        return 0;
    }

    // 3) read payload
    char *buf = (char *)malloc(sz);
    if (!buf)
    {
        close(sd);
        return -1;
    }
    int got = receiveDataInChunks(sd, buf, sz);
    close(sd);
    if (got != sz)
    {
        free(buf);
        return -1;
    }

    *outBuf = buf;
    *outLen = sz;
    return 0;
}

// send the final names blob to the client (size-prefixed)
static int send_names_blob(int con_sd, const char *blob, int len)
{
    const char *ok = "Success: Names ready";
    if (write(con_sd, ok, strlen(ok)) <= 0)
        return -1;
    usleep(10000);

    uint32_t net = htonl((uint32_t)len);
    if (write(con_sd, &net, sizeof(net)) != sizeof(net))
        return -1;
    usleep(10000);

    if (len > 0)
    {
        if (sendDataInChunks(con_sd, blob, len) != len)
            return -1;
    }
    return 0;
}

// --- S1: dispfnames handler ---
void handleDispfnames(int con_sd, char *commandArgs[], int *count)
{
    // Validate arg
    if (*count != 2 || strncmp(commandArgs[1], "~S1", 3) != 0)
    {
        const char *msg = "Error: dispfnames requires a valid ~S1 path (directory).";
        write(con_sd, msg, strlen(msg));
        return;
    }

    // Expand ~S1 to absolute on S1
    char baseS1[MAX_PATH];
    const char *home = getenv("HOME");
    if (!home)
        home = "/home/user";
    snprintf(baseS1, sizeof(baseS1), "%s/S1%s", home, commandArgs[1] + 3);

    // Prepare corresponding absolute directories on S2/S3/S4
    char baseS2[MAX_PATH], baseS3[MAX_PATH], baseS4[MAX_PATH];
    snprintf(baseS2, sizeof(baseS2), "%s/S2%s", home, commandArgs[1] + 3);
    snprintf(baseS3, sizeof(baseS3), "%s/S3%s", home, commandArgs[1] + 3);
    snprintf(baseS4, sizeof(baseS4), "%s/S4%s", home, commandArgs[1] + 3);

    // ----- 1) Local .c list on S1 (non-recursive) -----
    char **cList = NULL;
    int cCount = 0;
    collect_names_one_dir(baseS1, ".c", &cList, &cCount);
    int cLen = 0;
    char *cBlob = join_names(cList, cCount, &cLen);

    // free cList array (but keep blob)
    for (int i = 0; i < cCount; i++)
        free(cList[i]);
    free(cList);

    // ----- 2) Remote .pdf from S2 -----
    char *pdfBlob = NULL;
    int pdfLen = 0;
    if (fetch_names_from_peer(server2_ip, server2_port, baseS2, ".pdf", &pdfBlob, &pdfLen) < 0)
    {
        // treat as empty if peer fails
        pdfBlob = NULL;
        pdfLen = 0;
    }

    // ----- 3) Remote .txt from S3 -----
    char *txtBlob = NULL;
    int txtLen = 0;
    if (fetch_names_from_peer(server3_ip, server3_port, baseS3, ".txt", &txtBlob, &txtLen) < 0)
    {
        txtBlob = NULL;
        txtLen = 0;
    }

    // ----- 4) Remote .zip from S4 -----
    char *zipBlob = NULL;
    int zipLen = 0;
    if (fetch_names_from_peer(server4_ip, server4_port, baseS4, ".zip", &zipBlob, &zipLen) < 0)
    {
        zipBlob = NULL;
        zipLen = 0;
    }

    // ----- 5) Concatenate in required order: .c, .pdf, .txt, .zip -----
    int totalLen = cLen + pdfLen + txtLen + zipLen;
    char *finalBlob = NULL;
    if (totalLen > 0)
    {
        finalBlob = (char *)malloc(totalLen);
        if (!finalBlob)
        {
            const char *msg = "Error: Memory allocation failed.";
            write(con_sd, msg, strlen(msg));
            if (cBlob)
                free(cBlob);
            if (pdfBlob)
                free(pdfBlob);
            if (txtBlob)
                free(txtBlob);
            if (zipBlob)
                free(zipBlob);
            return;
        }
        int off = 0;
        if (cLen)
        {
            memcpy(finalBlob + off, cBlob, cLen);
            off += cLen;
        }
        if (pdfLen)
        {
            memcpy(finalBlob + off, pdfBlob, pdfLen);
            off += pdfLen;
        }
        if (txtLen)
        {
            memcpy(finalBlob + off, txtBlob, txtLen);
            off += txtLen;
        }
        if (zipLen)
        {
            memcpy(finalBlob + off, zipBlob, zipLen);
            off += zipLen;
        }
    }

    // send to client
    if (send_names_blob(con_sd, finalBlob ? finalBlob : "", totalLen) != 0)
    {
        // fallthrough; client will handle
    }

    // cleanup
    if (cBlob)
        free(cBlob);
    if (pdfBlob)
        free(pdfBlob);
    if (txtBlob)
        free(txtBlob);
    if (zipBlob)
        free(zipBlob);
    if (finalBlob)
        free(finalBlob);
}

// Function to handle client
void prcclient(int con_sd)
{
    // Define command and commandArgs to tokenize user input
    char command[MAX_BUFFER];
    char *commandArgs[MAX_COMMAND_ARGS];
    int bytes;
    while (1)
    {
        int count = 0;
        memset(command, 0, MAX_BUFFER);
        bytes = read(con_sd, command, MAX_BUFFER - 1);
        // Error if read fails
        if (bytes < 0)
        {
            printf("\nClient Disconnected (read error).\n");
            break;
        }
        if (bytes == 0)
        {
            printf("\nClient Disconnected (connection closed).\n");
            break;
        }
        // Add string terminator
        command[bytes] = '\0';
        // Tokenize user entered command
        if (!tokenizeCommand(command, commandArgs, &count))
        {
            char *errorMsg = "Error: Command tokenization failed.\n";
            write(con_sd, errorMsg, strlen(errorMsg));
            break;
        }
        // If command is uploadf
        if (strcmp(commandArgs[0], "uploadf") == 0)
        {
            // Handle uploadf command
            handleUploadf(con_sd, commandArgs, &count);
        }
        // If command is downlf
        else if (strcmp(commandArgs[0], "downlf") == 0)
        {
            // Handle downlf command
            handleDownlf(con_sd, commandArgs, &count);
        }
        // If command is removef
        else if (strcmp(commandArgs[0], "removef") == 0)
        {
            // Handle removef command
            handleRemovef(con_sd, commandArgs, &count);
        }
        // If command is downltar
        else if (strcmp(commandArgs[0], "downltar") == 0)
        {
            // Handle downltar command
            handleDownltar(con_sd, commandArgs, &count);
        }
        // If command is dispfnames
        else if (strcmp(commandArgs[0], "dispfnames") == 0)
        {
            // Handle dispfnames command
            handleDispfnames(con_sd, commandArgs, &count);
        }
        // Free the commandArgs array
        for (int i = 0; i < count; i++)
        {
            if (commandArgs[i])
            {
                free(commandArgs[i]);
                commandArgs[i] = NULL;
            }
        }
    }
    // Final clean-up
    for (int i = 0; i < MAX_COMMAND_ARGS; i++)
    {
        if (commandArgs[i])
        {
            free(commandArgs[i]);
        }
    }
    // Close the connection
    close(con_sd);
}

// Main function
int main(int argc, char *argv[])
{
    // Socket variable
    int lis_sd, con_sd, portNumber;
    socklen_t len;
    struct sockaddr_in servAdd;
    int pid;
    // Error if file not run correctly
    if (argc != 8)
    {
        fprintf(stderr, "Usage: %s <Server1_Port> <Server2_IP> <Server2_Port> <Server3_IP> <Server3_Port> <Server4_IP> <Server4_Port>\n", argv[0]);
        exit(0);
    }
    // Assign server 2-4 values to respected variables
    server2_ip = argv[2];
    sscanf(argv[3], "%d", &server2_port);
    server3_ip = argv[4];
    sscanf(argv[5], "%d", &server3_port);
    server4_ip = argv[6];
    sscanf(argv[7], "%d", &server4_port);

    // socket() call
    if ((lis_sd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        fprintf(stderr, "Could not create socket\n");
        exit(1);
    }

    // Add port number and IP address to servAdd before invoking the bind() system call
    servAdd.sin_family = AF_INET;
    // Add the IP address of the machine
    servAdd.sin_addr.s_addr = htonl(INADDR_ANY);
    // htonl: Host to Network Long : Converts host byte order to network byte order
    sscanf(argv[1], "%d", &portNumber);
    servAdd.sin_port = htons((uint16_t)portNumber); // Add the port number entered by the user

    // bind() call
    bind(lis_sd, (struct sockaddr *)&servAdd, sizeof(servAdd));
    // listen max 5 client
    listen(lis_sd, 5);

    while (1)
    {
        // Accept client connection
        con_sd = accept(lis_sd, (struct sockaddr *)NULL, NULL);
        // Fork for client
        pid = fork();
        // Child process service client request using prcclient
        if (pid == 0)
        {
            // Close the listing socket, as not needed
            close(lis_sd);
            prcclient(con_sd);
            // Child terminate when client is done executing command
            exit(0);
        }
        // Parent continues to accept new connection
        else if (pid > 0)
        {
            // Close the connection socket, as not needed
            close(con_sd);
        }
        // Error if fork fails
        else
        {
            perror("\nFork Failed.\n");
        }
    }
    // Close the listing socket
    close(lis_sd);
    return 0;
}
