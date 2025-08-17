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
#define SUPPORTED_EXT ".zip"

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

// Helper function to extract path
void extractPath(char *path)
{
    char *lastSlash = strrchr(path, '/');
    // Treminate the string at the last slash to remove file name
    if (lastSlash != NULL)
    {
        *lastSlash = '\0';
    }
}

// Helper function to verify the existence of file
int validateFileExist(char *filename)
{
    struct stat st;
    // Return 0, if file does not exist
    if (stat(filename, &st) != 0)
    {
        perror(filename);
        return 0;
    }
    // Return 1 if file not exist
    return 1;
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

// Helper function to create directory on server
int createDirectory(char *path)
{
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
    // Check the path contains /S4/
    if (strstr(tempPath, "/S4/"))
    {
        // Skip "/S4"
        baseEnd = strstr(tempPath, "/S4/") + 3;
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

// Function to handle uploadf command
void handleUploadf(int con_sd, char *commandArgs[])
{
    // Get File path and name
    char fileCommand[256];
    snprintf(fileCommand, sizeof(fileCommand), "%s", commandArgs[1]);
    // Read file size from server1(network bytes)
    uint32_t networkFileSize;
    int bytes = read(con_sd, &networkFileSize, sizeof(uint32_t));
    // Use ntohl to convert network bytes to host bytes
    int fileSize = ntohl(networkFileSize);
    // Error if file size is invalid
    if (bytes <= 0 || fileSize <= 0 || fileSize > MAX_FILE_SIZE)
    {
        char *errorMsg = "Error: Invalid file size server";
        write(con_sd, errorMsg, strlen(errorMsg));
        return;
    }
    // Allocate memory for file based on size
    char *fileData = malloc(fileSize + 1);
    // Error if memory allocation fails
    if (!fileData)
    {
        char *errorMsg = "Error: Memory allocation failed";
        write(con_sd, errorMsg, strlen(errorMsg));
        return;
    }
    // Variable to store file name and path
    char destPath[MAX_PATH];
    char filePathAndName[MAX_PATH];
    memset(filePathAndName, 0, sizeof(filePathAndName));
    strcpy(filePathAndName, fileCommand);
    memset(destPath, 0, sizeof(destPath));
    strcpy(destPath, fileCommand);
    // Extract only the path from destPath
    extractPath(destPath);
    // Create the directory for the dest path
    if (createDirectory(destPath) == -1)
    {
        free(fileData);
        char *errorMsg = "\nError: Failed to create directory on server.\n";
        write(con_sd, errorMsg, strlen(errorMsg));
        return;
    }
    // Receive file data in chunks
    int totalReceived = receiveDataInChunks(con_sd, fileData, fileSize);
    // Error if entire file is not read/received
    if (totalReceived != fileSize)
    {
        free(fileData);
        char *errorMsg = "Error: Failed to receive complete file data";
        write(con_sd, errorMsg, strlen(errorMsg));
        return;
    }
    // Use open to create the file
    int fd = open(filePathAndName, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    // Error if open fails
    if (fd < 0)
    {
        free(fileData);
        char *errorMsg = "Error: Failed to create file on Server2";
        write(con_sd, errorMsg, strlen(errorMsg));
        return;
    }
    // Write the file on server
    int bytesWritten = write(fd, fileData, fileSize);
    // Close the file
    close(fd);
    // Free file buffer
    free(fileData);
    // Error if file not written completely
    if (bytesWritten != fileSize)
    {
        char *errorMsg = "Error: Failed to write complete file on Server2";
        write(con_sd, errorMsg, strlen(errorMsg));
        return;
    }
    // Send success response
    char successMsg[MAX_BUFFER];
    snprintf(successMsg, sizeof(successMsg), "File uploaded successfully to Server");
    write(con_sd, successMsg, strlen(successMsg));
}

// Function to handle removef command
void handleRemovef(int con_sd, char *commandArgs[])
{
    char response[MAX_BUFFER];
    // Validate file exist on server4
    if (!validateFileExist(commandArgs[1]))
    {
        snprintf(response, sizeof(response), "File does not exist on Server");
        write(con_sd, response, strlen(response));
        return;
    }
    // Remove the file using unlink
    unlink(commandArgs[1]);
    snprintf(response, sizeof(response), "File removed successfully from Server");
    // Send respond to server1
    write(con_sd, response, strlen(response));
}

// Function to collect names of files with a specific extension in a directory
static int collect_names_one_dir_peer(const char *dir, const char *ext, char ***outList, int *outCount)
{
    // Open the directory
    DIR *dp = opendir(dir);
    *outList = NULL;
    *outCount = 0;
    if (!dp)
        return -1;
    // Check if the directory is empty
    struct dirent *de;
    int cap = 16, n = 0;
    char **arr = (char **)malloc(cap * sizeof(char *));
    if (!arr)
    {
        closedir(dp);
        return -1;
    }
    // Iterate through directory entries
    while ((de = readdir(dp)) != NULL)
    {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;
        const char *dot = strrchr(de->d_name, '.');
        if (!dot || strcmp(dot, ext) != 0)
            continue;
        if (n == cap)
        {
            cap *= 2;
            char **tmp = (char **)realloc(arr, cap * sizeof(char *));
            if (!tmp)
            {
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
        {
            for (int i = 0; i < n; i++)
                free(arr[i]);
            free(arr);
            closedir(dp);
            return -1;
        }
        n++;
    }
    closedir(dp);

    if (n > 1)
        qsort(arr, n, sizeof(char *), cmpstr);

    *outList = arr;
    *outCount = n;
    return 0;
}
// Function to join names into a single string with newline separators
// The caller is responsible for freeing the returned string
static char *join_names_peer(char **names, int count, int *outLen)
{
    size_t total = 0;
    for (int i = 0; i < count; i++)
        total += strlen(names[i]) + 1;
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

static void handleDispfnames(int con_sd, char *commandArgs[])
{
    // command: listnames <abs_dir> <ext>
    if (!commandArgs[1] || !commandArgs[2] || strcmp(commandArgs[2], SUPPORTED_EXT) != 0)
    {
        const char *msg = "Error: Unsupported extension for this server";
        write(con_sd, msg, strlen(msg));
        return;
    }
    const char *dir = commandArgs[1];

    char **names = NULL;
    int count = 0;
    // if dir missing, we still succeed with empty list
    collect_names_one_dir_peer(dir, SUPPORTED_EXT, &names, &count);
    int len = 0;
    char *blob = join_names_peer(names, count, &len);
    for (int i = 0; i < count; i++)
        free(names[i]);
    free(names);

    const char *ok = "Success: Names ready";
    write(con_sd, ok, strlen(ok));
    usleep(10000);

    uint32_t net = htonl((uint32_t)len);
    write(con_sd, &net, sizeof(net));
    usleep(10000);

    if (len > 0 && blob)
    {
        sendDataInChunks(con_sd, blob, len);
        free(blob);
    }
}

// Function to handle server request
void handleRequest(int con_sd)
{
    // Define command and commandArgs to tokenize sever command
    char command[MAX_BUFFER];
    char *commandArgs[MAX_COMMAND_ARGS];
    int bytes;
    while (1)
    {
        int count = 0;
        memset(command, 0, MAX_BUFFER);
        bytes = read(con_sd, command, MAX_BUFFER);
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
        // Tokenize user received from server1
        if (!tokenizeCommand(command, commandArgs, &count))
        {
            char *errorMsg = "Error: Command tokenization failed";
            write(con_sd, errorMsg, strlen(errorMsg));
            break;
        }
        // If command is uploadf
        if (strcmp(commandArgs[0], "uploadf") == 0)
        {
            handleUploadf(con_sd, commandArgs);
        }
        // If command is removef
        else if (strcmp(commandArgs[0], "removef") == 0)
        {
            handleRemovef(con_sd, commandArgs);
        }
        // If command is dispfnames
        else if (strcmp(commandArgs[0], "dispfnames") == 0)
        {
            handleDispfnames(con_sd, commandArgs);
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
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <Port>\n", argv[0]);
        exit(0);
    }
    // socket() sytem call
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

    // bind() system call
    bind(lis_sd, (struct sockaddr *)&servAdd, sizeof(servAdd));
    // listen max 5 client
    listen(lis_sd, 5);

    while (1)
    {
        // Accept client connection
        con_sd = accept(lis_sd, (struct sockaddr *)NULL, NULL);
        // Fork for client
        pid = fork();
        // Child process service client request using handleRequest
        if (pid == 0)
        {
            // Close the listing socket, as not needed
            close(lis_sd);
            handleRequest(con_sd);
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