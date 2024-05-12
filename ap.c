#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <stdint.h>

#define PORT 8080
#define BUFFER_SIZE 1024
#define MAX_PAYLOAD_SIZE 512 

// Structure for simulated frame with payload support
typedef struct __attribute__((packed)) {
    uint8_t type;
    uint8_t subtype;
    uint16_t duration_id;
    char address1[18];
    char address2[18];
    char address3[18];
    unsigned int fcs;
    char payload[MAX_PAYLOAD_SIZE];
} SimulatedFrame;

uint32_t generate32bitChecksum(const char* valueToConvert) {
    uint32_t checksum = 0;
    while (*valueToConvert) {
        checksum += *valueToConvert++;
        checksum += (checksum << 10);
        checksum ^= (checksum >> 6);
    }
    checksum += (checksum << 3);
    checksum ^= (checksum >> 11);
    checksum += (checksum << 15);
    return checksum;
}

uint32_t getCheckSumValue(const void *ptr, size_t size) {
    return generate32bitChecksum((const char*)ptr);
}

uint32_t getCheckSum(const void *ptr, size_t size, ssize_t bytesToSkipFromStart, size_t bytesToSkipFromEnd) {
const unsigned char *byte = (const unsigned char *)ptr;
 char binaryString[9]; 
// binaryString[] is a logical representation of 1 byte. Each character in it represents 1 bit.
// Do not confuse with the size of character in C language (which is 1 byte). This is just a representation. char binaryString[9]; // One additional character for the null terminator
binaryString[8] = '\0'; // Null terminator definition
 
char *buffer = malloc(1); // Allocates space for an empty string (1 byte for the null terminator) buffer[0] = '\0'; // Initializes an empty string
 
for (size_t i = 1; i <= size; i++) { for (int j = 7; j >= 0; j--) {
int bit = (byte[i - 1] >> j) & 1;
binaryString[7 - j] = bit + '0'; // Converts bit to character '0' or '1'
}
buffer = realloc (buffer, strlen(buffer) + strlen(binaryString) + 1); // Resizes buffer to fit the concatenated result
strcat(buffer, binaryString);
}
buffer[strlen(buffer)-(bytesToSkipFromEnd*8)] = '\0';
memmove(buffer, buffer + (bytesToSkipFromStart*8), strlen(buffer) - (bytesToSkipFromStart*8) + 1); //+1 for null terminator
// printf("\nGenerated string: %s\n", buffer);
// printf("\nSize of generated string in bytes: %zu\n", strlen(buffer)/8);
 
uint32_t checkSumValue = generate32bitChecksum(buffer);
free(buffer); // Freeing memory allocated by malloc.
return checkSumValue;
}

void processFrame(SimulatedFrame *frame, struct sockaddr_in *cliAddr, int sockfd) {
    char responseMsg[BUFFER_SIZE];

    // Validate frame type
    if (frame->type != 0x00 && frame->type != 0x01 && frame->type != 0x02 && frame->type != 0x10) {
        printf("Invalid Frame Type received.\n");
        snprintf(responseMsg, BUFFER_SIZE, "Error: Invalid Frame Type");
        sendto(sockfd, responseMsg, strlen(responseMsg), 0, (struct sockaddr *)cliAddr, sizeof(*cliAddr));
        return;
    }

    // Reset FCS for checksum calculation
    unsigned int originalFcs = frame->fcs;
    frame->fcs = 0; 

    size_t frameSize = sizeof(SimulatedFrame) - sizeof(frame->fcs) - MAX_PAYLOAD_SIZE;
    if(frame->type == 0x10) { // Adjust for Data Frame payload length
        frameSize += strlen(frame->payload);
    }

    uint32_t calculatedFcs = getCheckSumValue(frame, frameSize);

    sleep(4); // to recreate retry scenario, ap doesnot respond scenario

    // Checksum verification
    if (originalFcs != calculatedFcs) {
        printf("Error: FCS Mismatch.\n");
        snprintf(responseMsg, BUFFER_SIZE, "Error: FCS Mismatch");
        sendto(sockfd, responseMsg, strlen(responseMsg), 0, (struct sockaddr *)cliAddr, sizeof(*cliAddr));
        return;
    }
    else {
    // Process frame based on type
    switch(frame->type) {
        case 0x00: // Association Request
            printf("Association Request received.\n");
            snprintf(responseMsg, BUFFER_SIZE, "Association Response: Accepted");
            break;
        case 0x01: // Probe Request
            printf("Probe Request received.\n");
            snprintf(responseMsg, BUFFER_SIZE, "Probe Response: Accepted");
            break;
        case 0x02: // RTS
            printf("RTS received.\n");
            snprintf(responseMsg, BUFFER_SIZE, "CTS");
            break;
        case 0x10: // Data Frame
            // If checksum is verified, process the Data Frame
            printf("Data Frame received. Payload: %s\n", frame->payload);
            snprintf(responseMsg, BUFFER_SIZE, "ACK");
            break;
        default:
            printf("Unknown Frame Type received.\n");
            snprintf(responseMsg, BUFFER_SIZE, "Unknown Frame Type");
            break;
    }
    }

    // Send response
    sleep(1);
    sendto(sockfd, responseMsg, strlen(responseMsg), 0, (struct sockaddr *)cliAddr, sizeof(*cliAddr));
    printf("Response sent: %s\n", responseMsg);
}



int main(void) {
    int sockfd;
    struct sockaddr_in servAddr, cliAddr;

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&servAddr, 0, sizeof(servAddr));
    servAddr.sin_family = AF_INET;
    servAddr.sin_addr.s_addr = INADDR_ANY;
    servAddr.sin_port = htons(PORT);

    if (bind(sockfd, (const struct sockaddr *)&servAddr, sizeof(servAddr)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    printf("Access Point is listening...\n");

    while (1) {
        SimulatedFrame recvFrame;
        socklen_t cliLen = sizeof(cliAddr);
        ssize_t n = recvfrom(sockfd, &recvFrame, sizeof(recvFrame), 0, (struct sockaddr *)&cliAddr, &cliLen);
        if (n > 0) {
            processFrame(&recvFrame, &cliAddr, sockfd);
        }
    }

    close(sockfd);
    return 0;
}
