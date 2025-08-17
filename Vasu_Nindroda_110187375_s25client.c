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

// Global constant
#define MAX_BUFFER 2048
#define MAX_COMMAND_ARGS 5
#define CHUNK_SIZE 8192
#define MAX_FILE_SIZE (50 * 1024 * 1024)

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

// Helper function to remove extra spaces from start and end
void trim(char *str)
{
    int start = 0, end = strlen(str) - 1;
    while (str[start] == ' ')
    {
        start++;
    }
    while (end > start && str[end] == ' ')
    {
        end--;
    }
    int index = 0;
    while (start <= end)
    {
        str[index] = str[start];
        index++;
        start++;
    }
    // Add string terminator
    str[index] = '\0';
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

// Helper function to validate file extension
int isValidExtension(char *filename, char *allowedExts[], int numExts)
{
    char ext[32];
    // Extract the extension from filename and store to ext
    snprintf(ext, sizeof(ext), "%s", getFileExtension(filename));
    // No extension found
    if (strlen(ext) == 0)
    {
        return 0;
    }
    // Loop through allowed number of extension
    for (int i = 0; i < numExts; i++)
    {
        // Return 1, if there is a match
        if (strcmp(ext, allowedExts[i]) == 0)
        {
            return 1;
        }
    }
    // Return 0, if there is no match
    return 0;
}

// Helper function to verify user entered path
int isValidPath(char *command)
{
    // Must begin with ~S1
    if (!(strncmp(command, "~S1", 3) == 0))
    {
        // Return 0, for incorrect path
        return 0;
    }
    // Return 1, for correct path
    return 1;
}

// Function to read input from user
int readInput(char *input, int maxLen)
{
    // Using read system call and the FD is STD INPUT
    int bytes = read(STDIN_FILENO, input, maxLen - 1);
    // Error if read fails
    if (bytes < 0)
    {
        printf("\nRead input failed.\n");
        exit(1);
    }
    // Add string terminator
    input[bytes] = '\0';
    int len = strlen(input);
    // Remove trailing newline character if present
    if (len > 0 && input[len - 1] == '\n')
    {
        input[len - 1] = '\0';
    }
    // If user has entered something, return 1
    for (int i = 0; i < strlen(input); i++)
    {
        if (input[i] != ' ' && input[i] != '\t')
        {
            return 1;
        }
    }
    // Return 0 for only space
    return 0;
}

// Function to validate command syntax, return 0 for invalid, 1 for valid command
int validateCommandSyntax(char *input, char *commandArgs[], int *count)
{
    // Remove spaces from command
    trim(input);
    char copyInput[MAX_BUFFER];
    // Copy input to copyInput
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
    // Define allowed extensions
    char *uploadfExts[] = {".c", ".pdf", ".txt", ".zip"};
    char *downltarExts[] = {".c", ".pdf", ".txt"};
    char *downlfExts[] = {".c", ".pdf", ".txt"};

    // If command is uploadf
    if (strcmp(commandArgs[0], "uploadf") == 0)
    {
        // Return 0(Error), if there are less than 3 or greater than 5 portion
        if (*count < 3 || *count > 5)
        {
            return 0;
        }
        // Get number of files in command by subtracting 2 from total count.[uploadf and destPath]
        int numFiles = *count - 2;
        // Return 0(Error), if there are less than 1 or greater than 3 files
        if (numFiles < 1 || numFiles > 3)
        {
            printf("\nError: uploadf requires 1 to 3 files\n");
            return 0;
        }
        // Copy the path to lastArg
        char lastArg[256];
        snprintf(lastArg, sizeof(lastArg), "%s", commandArgs[*count - 1]);
        // Verify the path
        if (!isValidPath(lastArg))
        {
            printf("\nError: Last argument must be a valid destination path.\n");
            return 0;
        }
        // Validate file extension for uploadf
        for (int i = 1; i < *count - 1; i++)
        {
            if (!isValidExtension(commandArgs[i], uploadfExts, 4))
            {
                printf("\nError: Invalid file extension for uploadf.\n");
                return 0;
            }
        }
    }
    // If command is downlf
    else if (strcmp(commandArgs[0], "downlf") == 0)
    {
        // Return 0(Error), if there are less than 2 or greater than 3 portion
        if (*count < 2 || *count > 3)
        {
            return 0;
        }
        // Validate file extension for downlf
        for (int i = 1; i < *count; i++)
        {
            if (!isValidExtension(commandArgs[i], downlfExts, 3))
            {
                printf("\nError: Invalid file extension for downlf.\n");
                return 0;
            }
        }
        // Verify the paths in command
        for (int i = 1; i < *count; i++)
        {
            if (!isValidPath(commandArgs[i]))
            {
                printf("\nError: Command argument must be a valid path.\n");
                return 0;
            }
        }
    }
    // If command is removef
    else if (strcmp(commandArgs[0], "removef") == 0)
    {
        // Return 0(Error), if there are less than 2 or greater than 3 portion
        if (*count < 2 || *count > 3)
        {
            return 0;
        }
        // Verify the paths in command
        for (int i = 1; i < *count; i++)
        {
            if (!isValidPath(commandArgs[i]))
            {
                printf("\nError: Command argument must be a valid path.\n");
                return 0;
            }
        }
    }
    // If command is downltar
    else if (strcmp(commandArgs[0], "downltar") == 0)
    {
        // Return 0(Error), if there are exactly 1 or greater than 2 portion
        if (*count == 1 || *count > 2)
        {
            return 0;
        }
        // Return 0(Error), if the second portion is not extension
        char *ext = commandArgs[1];
        if (ext[0] != '.')
        {
            printf("\nError: downltar requires an extension.\n");
            return 0;
        }
        int validExt = 0;
        // Validate the extension
        for (int i = 0; i < 3; i++)
        {
            if (strcmp(ext, downltarExts[i]) == 0)
            {
                validExt = 1;
                break;
            }
        }
        // Return 0(Error), if the extension is invalid
        if (!validExt)
        {
            printf("\nError: Invalid extension for downltar.\n");
            return 0;
        }
    }
    // If command is dispfnames
    else if (strcmp(commandArgs[0], "dispfnames") == 0)
    {
        if (*count > 2)
        {
            return 0;
        }
    }
    // If the entered command is not acceptable
    else
    {
        return 0;
    }
    // If all condition are correct, return 1
    return 1;
}

// Main method
int main(int argc, char *argv[])
{
    // Define input and commandArgs
    char input[MAX_BUFFER];
    char *commandArgs[MAX_COMMAND_ARGS];
    int client_sd, portNumber;
    struct sockaddr_in servAdd;
    // Error if file not run correctly
    if (argc != 3)
    {
        printf("Call model:%s <IP> <Port#>\n", argv[0]);
        exit(1);
    }
    // Socket call
    if ((client_sd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        fprintf(stderr, "\nError: Cannot create socket\n");
        exit(1);
    }

    // ADD the server's PORT NUMBER AND IP ADDRESS TO THE sockaddr_in object
    servAdd.sin_family = AF_INET;
    sscanf(argv[2], "%d", &portNumber);
    servAdd.sin_port = htons((uint16_t)portNumber);

    // inet_pton() is used to convert the IP address in text into binary
    if (inet_pton(AF_INET, argv[1], &servAdd.sin_addr) < 0)
    {
        fprintf(stderr, "\nError: inet_pton() has failed\n");
        exit(1);
    }
    // Connect call
    if (connect(client_sd, (struct sockaddr *)&servAdd, sizeof(servAdd)) < 0)
    { // Connect()
        fprintf(stderr, "\nError: connect() failed, exiting\n");
        exit(1);
    }

    // Print the available command menu
    printf("\nConnected to server\n");
    printf("\nAvailable commands:\n");
    printf("\n1. uploadf [filename1] [filename2] [filename3] destination_path\n");
    printf("\n2. downlf [filename1_path] [filename2_path]\n");
    printf("\n3. removef [filename1_path] [filename2_path]\n");
    printf("\n4. downltar [file_extension]\n");
    printf("\n5. dispfnames pathname\n");
    printf("nNote: The destination_path must start with ~S1\n");
    printf("\nType 'quit' to exit\n");

    // Run the infinite loop
    while (1)
    {
        int count = 0;
        printf("s25client$ ");
        fflush(stdout);
        // Clearing buffer of commandArgs
        memset(commandArgs, 0, sizeof(commandArgs));
        // Calling readInput to read user input, and proceeding if valid input
        if (!readInput(input, MAX_BUFFER))
        {
            continue;
        }
        // If user enter quit, then terminate
        if (strcmp(input, "quit") == 0)
        {
            break;
        }
        // Validate command syntax
        if (!validateCommandSyntax(input, commandArgs, &count))
        {
            printf("\nError: Invalid command syntax\n");
            // Free commandArgs
            for (int i = 0; i < count; i++)
            {
                if (commandArgs[i])
                {
                    free(commandArgs[i]);
                    commandArgs[i] = NULL;
                }
            }
            continue;
        }
        // Copy the user input to commandToSend
        char commandToSend[MAX_BUFFER];
        strcpy(commandToSend, input);
        int success = 1;
        // If command is uploadf
        if (strcmp(commandArgs[0], "uploadf") == 0)
        {
            // Validate the enterd file exist in client pwd
            for (int i = 1; i < count - 1; i++)
            {
                if (!validateFileExist(commandArgs[i]))
                {
                    printf("File %s does not exist\n", commandArgs[i]);
                    success = false;
                    break;
                }
            }
            if (!success)
            {
                // Free commandArgs
                for (int i = 0; i < count; i++)
                {
                    if (commandArgs[i])
                    {
                        free(commandArgs[i]);
                        commandArgs[i] = NULL;
                    }
                }
                continue;
            }
            // Frist send the command to server using write
            if (write(client_sd, input, strlen(input)) <= 0)
            {
                printf("\nError: Failed to send command to server\n");
                // Free commandArgs
                for (int i = 0; i < count; i++)
                {
                    if (commandArgs[i])
                    {
                        free(commandArgs[i]);
                        commandArgs[i] = NULL;
                    }
                }
                continue;
            }
            // Then send each file
            for (int i = 1; i < count - 1; i++)
            {
                // Get the file size
                struct stat st;
                stat(commandArgs[i], &st);
                int fileSize = st.st_size;
                // Send file size first using write
                if (write(client_sd, &fileSize, sizeof(int)) <= 0)
                {
                    printf("\nError: Failed to send file size for '%s'\n", commandArgs[i]);
                    break;
                }
                // Allocate buffer for file based on size
                char *fileBuffer = malloc(fileSize + 1);
                // Error if buffer allocation using malloc fails
                if (!fileBuffer)
                {
                    printf("\nError: Memory allocation failed for '%s'\n", commandArgs[i]);
                    break;
                }
                // Open the file
                int fd = open(commandArgs[i], O_RDONLY);
                // Error if open file fails
                if (fd < 0)
                {
                    printf("\nError: Failed to open file: %s\n", commandArgs[i]);
                    free(fileBuffer);
                    break;
                }
                // Read the file content
                int bytesRead = read(fd, fileBuffer, fileSize);
                // Close the file
                close(fd);
                // Error if file not read fully
                if (bytesRead != fileSize)
                {
                    printf("\nError: Failed to read file: %s\n", commandArgs[i]);
                    free(fileBuffer);
                    break;
                }
                // Send file data in chunks
                int sentBytes = sendDataInChunks(client_sd, fileBuffer, fileSize);
                // Error if all data is not sent
                if (sentBytes != fileSize)
                {
                    printf("\nError: Failed to send file '%s'\n", commandArgs[i]);
                    free(fileBuffer);
                    break;
                }
                // Free the buffer
                free(fileBuffer);
            }
            // Receive response from server for each file
            for (int i = 1; i < count - 1; i++)
            {
                char response[MAX_BUFFER];
                // Read response
                int responseLen = read(client_sd, response, MAX_BUFFER - 1);
                // Error if read response fails
                if (responseLen <= 0)
                {
                    printf("Failed to receive response for file %d\n", i);
                    break;
                }
                response[responseLen] = '\0';
                // Print the response
                printf("Server response for file %d: %s\n", i, response);
            }
        }
        // If command is downlf
        else if (strcmp(commandArgs[0], "downlf") == 0)
        {
            // Frist send the command to server using write
            if (write(client_sd, input, strlen(input)) <= 0)
            {
                printf("\nError: Failed to send command to server\n");
                for (int i = 0; i < count; i++)
                {
                    if (commandArgs[i])
                    {
                        free(commandArgs[i]);
                        commandArgs[i] = NULL;
                    }
                }
                continue;
            }
            // For each file get response and write file on client pwd
            for (int i = 1; i < count; i++)
            {
                // Read initial response from server
                char response[MAX_BUFFER];
                int responseLen = read(client_sd, response, MAX_BUFFER - 1);
                // If there is error in reading response from server
                if (responseLen <= 0)
                {
                    printf("\nError: No response from server\n");
                    for (int i = 0; i < count; i++)
                    {
                        if (commandArgs[i])
                        {
                            free(commandArgs[i]);
                            commandArgs[i] = NULL;
                        }
                    }
                    continue;
                }
                response[responseLen] = '\0';
                // If there response contains error message from server
                if (strstr(response, "Error:") != NULL || strstr(response, "File does not exist") != NULL)
                {
                    // Print the server error message
                    printf("%s\n", response);
                    for (int i = 0; i < count; i++)
                    {
                        if (commandArgs[i])
                        {
                            free(commandArgs[i]);
                            commandArgs[i] = NULL;
                        }
                    }
                    continue;
                }
                // Read file name from server
                char fileName[512];
                memset(fileName, 0, sizeof(fileName));
                if (read(client_sd, fileName, sizeof(fileName)) < 0)
                {
                    printf("Failed to read file name from server.\n");
                    continue;
                }
                // Read file size from server(network bytes)
                uint32_t networkFileSize;
                int bytes = read(client_sd, &networkFileSize, sizeof(uint32_t));
                // Use ntohl to convert network bytes to host bytes
                int fileSize = ntohl(networkFileSize);
                // Error if file size is invalid
                if (bytes <= 0 || fileSize <= 0 || fileSize > MAX_FILE_SIZE)
                {
                    char *errorMsg = "Error: Invalid file size";
                    printf("\n%s\n", errorMsg);
                    for (int i = 0; i < count; i++)
                    {
                        if (commandArgs[i])
                        {
                            free(commandArgs[i]);
                            commandArgs[i] = NULL;
                        }
                    }
                    continue;
                }
                // Allocate memory for file based on size
                char *fileData = malloc(fileSize + 1);
                // Error if memory allocation fails
                if (!fileData)
                {
                    char *errorMsg = "Error: Memory allocation failed";
                    printf("\n%s\n", errorMsg);
                    for (int i = 0; i < count; i++)
                    {
                        if (commandArgs[i])
                        {
                            free(commandArgs[i]);
                            commandArgs[i] = NULL;
                        }
                    }
                    continue;
                }
                // Receive file data in portion from server
                int totalReceived = receiveDataInChunks(client_sd, fileData, fileSize);
                // Error if entire file is not received
                if (totalReceived != fileSize)
                {
                    free(fileData);
                    char *errorMsg = "Error: Failed to receive complete file data";
                    printf("\n%s\n", errorMsg);
                    for (int i = 0; i < count; i++)
                    {
                        if (commandArgs[i])
                        {
                            free(commandArgs[i]);
                            commandArgs[i] = NULL;
                        }
                    }
                    continue;
                }
                // Create and write file On client
                int fd = open(fileName, O_CREAT | O_WRONLY | O_TRUNC, 0644);
                // Error if file creation fails
                if (fd < 0)
                {
                    free(fileData);
                    char *errorMsg = "Error: Failed to create file on Server2";
                    printf("\n%s\n", errorMsg);
                    for (int i = 0; i < count; i++)
                    {
                        if (commandArgs[i])
                        {
                            free(commandArgs[i]);
                            commandArgs[i] = NULL;
                        }
                    }
                    continue;
                }
                // Check file written completely or not
                int bytesWritten = write(fd, fileData, fileSize);
                // Close file
                close(fd);
                // Free file data buffer
                free(fileData);
                // Error if file is not written completely
                if (bytesWritten != fileSize)
                {
                    char *errorMsg = "Error: Failed to write complete file on Server2";
                    printf("\n%s\n", errorMsg);
                    unlink(fileName);
                    continue;
                }
                // Print success message
                printf("File %s downloaded successfully\n", fileName);
            }
        }
        // If command is removef
        else if (strcmp(commandArgs[0], "removef") == 0)
        {
            // Frist send the command to server using write
            if (write(client_sd, input, strlen(input)) <= 0)
            {
                printf("\nError: Failed to send command to server\n");
                for (int i = 0; i < count; i++)
                {
                    if (commandArgs[i])
                    {
                        free(commandArgs[i]);
                        commandArgs[i] = NULL;
                    }
                }
                continue;
            }
            // Read reponse for each file path
            for (int i = 1; i < count; i++)
            {
                char response[MAX_BUFFER];
                int responseLen = read(client_sd, response, MAX_BUFFER - 1);
                if (responseLen <= 0)
                {
                    printf("Failed to receive response for file %d\n", i);
                    break;
                }
                response[responseLen] = '\0';
                printf("Server response for file %d: %s\n", i, response);
            }
        }
        // If command is downltar
        else if (strcmp(commandArgs[0], "downltar") == 0)
        {
            // Send the command line as-is to S1
            if (write(client_sd, input, strlen(input)) <= 0)
            {
                printf("\nError: Failed to send command to server\n");
                // free args and continue
                for (int i = 0; i < count; i++)
                {
                    if (commandArgs[i])
                    {
                        free(commandArgs[i]);
                        commandArgs[i] = NULL;
                    }
                }
                continue;
            }

            // 1) Read initial status text
            char response[MAX_BUFFER];
            int responseLen = read(client_sd, response, MAX_BUFFER - 1);
            if (responseLen <= 0)
            {
                printf("\nError: No response from server\n");
                for (int i = 0; i < count; i++)
                {
                    if (commandArgs[i])
                    {
                        free(commandArgs[i]);
                        commandArgs[i] = NULL;
                    }
                }
                continue;
            }
            response[responseLen] = '\0';

            // If server reported an error, print it and bail
            if (strstr(response, "Error:") != NULL)
            {
                printf("%s\n", response);
                for (int i = 0; i < count; i++)
                {
                    if (commandArgs[i])
                    {
                        free(commandArgs[i]);
                        commandArgs[i] = NULL;
                    }
                }
                continue;
            }

            // 2) Read tar file name
            char tarName[256];
            memset(tarName, 0, sizeof(tarName));
            if (read(client_sd, tarName, sizeof(tarName)) <= 0)
            {
                printf("Error: Failed to read tar file name from server.\n");
                for (int i = 0; i < count; i++)
                {
                    if (commandArgs[i])
                    {
                        free(commandArgs[i]);
                        commandArgs[i] = NULL;
                    }
                }
                continue;
            }

            // 3) Read tar size (network byte order)
            uint32_t netSize;
            int b = read(client_sd, &netSize, sizeof(uint32_t));
            if (b != sizeof(uint32_t))
            {
                printf("Error: Failed to read tar size.\n");
                for (int i = 0; i < count; i++)
                {
                    if (commandArgs[i])
                    {
                        free(commandArgs[i]);
                        commandArgs[i] = NULL;
                    }
                }
                continue;
            }
            int tarSize = ntohl(netSize);
            if (tarSize <= 0 || tarSize > MAX_FILE_SIZE)
            {
                printf("Error: Invalid tar size.\n");
                for (int i = 0; i < count; i++)
                {
                    if (commandArgs[i])
                    {
                        free(commandArgs[i]);
                        commandArgs[i] = NULL;
                    }
                }
                continue;
            }

            // 4) Receive tar payload
            char *buf = (char *)malloc(tarSize);
            if (!buf)
            {
                printf("Error: Memory allocation failed for tar.\n");
                for (int i = 0; i < count; i++)
                {
                    if (commandArgs[i])
                    {
                        free(commandArgs[i]);
                        commandArgs[i] = NULL;
                    }
                }
                continue;
            }
            int got = receiveDataInChunks(client_sd, buf, tarSize);
            if (got != tarSize)
            {
                free(buf);
                printf("Error: Failed to receive full tar data.\n");
                for (int i = 0; i < count; i++)
                {
                    if (commandArgs[i])
                    {
                        free(commandArgs[i]);
                        commandArgs[i] = NULL;
                    }
                }
                continue;
            }

            // 5) Write tar to disk
            int fd = open(tarName, O_CREAT | O_WRONLY | O_TRUNC, 0644);
            if (fd < 0)
            {
                free(buf);
                printf("Error: Failed to create %s\n", tarName);
                for (int i = 0; i < count; i++)
                {
                    if (commandArgs[i])
                    {
                        free(commandArgs[i]);
                        commandArgs[i] = NULL;
                    }
                }
                continue;
            }
            // Write the tar data to the file
            int wrote = write(fd, buf, tarSize);
            close(fd);
            free(buf);
            if (wrote != tarSize)
            {
                printf("Error: Failed to write complete tar file.\n");
                unlink(tarName);
                for (int i = 0; i < count; i++)
                {
                    if (commandArgs[i])
                    {
                        free(commandArgs[i]);
                        commandArgs[i] = NULL;
                    }
                }
                continue;
            }

            printf("Tar downloaded: %s (%d bytes)\n", tarName, tarSize);
        }

        // If command is dispfnames
        else if (strcmp(commandArgs[0], "dispfnames") == 0)
        {
            // 1) Send command to S1
            if (write(client_sd, input, strlen(input)) <= 0)
            {
                printf("\nError: Failed to send command to server\n");
                // free args before continuing
                for (int i = 0; i < count; i++)
                {
                    if (commandArgs[i])
                    {
                        free(commandArgs[i]);
                        commandArgs[i] = NULL;
                    }
                }
                continue;
            }

            // 2) Read status
            char status[MAX_BUFFER];
            int sl = read(client_sd, status, sizeof(status) - 1);
            if (sl <= 0)
            {
                printf("Error: No response from server\n");
                goto done_disp;
            }
            status[sl] = '\0';
            if (strstr(status, "Error:"))
            {
                printf("%s\n", status);
                goto done_disp;
            }

            // 3) Read size
            uint32_t netSz;
            if (read(client_sd, &netSz, sizeof(netSz)) != sizeof(netSz))
            {
                printf("Error: Failed to read list size\n");
                goto done_disp;
            }
            int sz = ntohl(netSz);
            if (sz < 0 || sz > MAX_FILE_SIZE)
            {
                printf("Error: Invalid list size\n");
                goto done_disp;
            }

            // 4) Read payload and print
            if (sz == 0)
            {
                // No files; match "Displays the names ... to the PWD of the client"
                printf("(no matching files)\n");

                // printf("(no matching files)\n");
                goto done_disp;
            }

            char *buf = (char *)malloc(sz + 1);
            if (!buf)
            {
                printf("Error: Memory allocation failed\n");
                goto done_disp;
            }

            if (receiveDataInChunks(client_sd, buf, sz) != sz)
            {
                free(buf);
                printf("Error: Failed to receive names list\n");
                goto done_disp;
            }
            buf[sz] = '\0';

            // Print names directly to stdout (PWD)
            printf("%s", buf);
            free(buf);

        done_disp:; // label needs a statement
        }

        // Free commandArgs
        for (int i = 0; i < MAX_COMMAND_ARGS; i++)
        {
            if (commandArgs[i])
            {
                free(commandArgs[i]);
                commandArgs[i] = NULL;
            }
        }
    }
    // Close the connection
    close(client_sd);
    return 0;
}