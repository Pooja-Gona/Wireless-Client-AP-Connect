#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <time.h>
#include <stdint.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8080
#define BUFFER_SIZE 1024
#define MAX_PAYLOAD_SIZE 512
#define RETRY_LIMIT 3
#define ACK_WAIT_TIME 3 // seconds


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

uint32_t generate32bitChecksum(const char* valueToConvert);
uint32_t getCheckSumValue(const void *ptr, size_t size);
int waitForAckWithTimeout(int sockfd, int timeout);
void sendFramesWithRetries(int sockfd, struct sockaddr_in *servaddr);
void sendFrame(int sockfd, struct sockaddr_in *servaddr, SimulatedFrame *frame, uint8_t frameType, const char* payload);
int waitForResponse(int sockfd, const char *expectedResponse);
void sendIncorrectChecksumFrame(int sockfd, struct sockaddr_in *servaddr);
uint32_t getCheckSum(const void *ptr, size_t size, ssize_t bytesToSkipFromStart, size_t bytesToSkipFromEnd);


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


int waitForAckWithTimeout(int sockfd, int timeout) {
    fd_set readfds;
    struct timeval tv;
    FD_ZERO(&readfds);
    FD_SET(sockfd, &readfds);

    // Set timeout (ACK_WAIT_TIME seconds)
    tv.tv_sec = timeout;
    tv.tv_usec = 0;

    int ret = select(sockfd + 1, &readfds, NULL, NULL, &tv);
    if (ret > 0) {
        // Data is available to read
        return waitForResponse(sockfd, "ACK");
    }
    // Timeout or error
    return 0;
}

void sendFramesWithRetries(int sockfd, struct sockaddr_in *servaddr) {
    for (int frameNo = 1; frameNo <= 5; ++frameNo) {
        int retryCount = 0;
        while (retryCount < RETRY_LIMIT) {
            char dataPayload[BUFFER_SIZE];
            snprintf(dataPayload, sizeof(dataPayload), "Frame %d data payload", frameNo);
            SimulatedFrame frame;
            sendFrame(sockfd, servaddr, &frame, 0x10, dataPayload); // Send data frame

            if (waitForAckWithTimeout(sockfd, ACK_WAIT_TIME)) {
                printf("ACK received for Frame %d.\n", frameNo);
                break; // Move to next frame
            } else {
                printf("No ACK received for Frame %d. Retrying...\n", frameNo);
                retryCount++;
            }

            if (retryCount == RETRY_LIMIT) {
                printf("No ACK received for Frame %d after %d retries. Access point may not be available.\n", frameNo, RETRY_LIMIT);
            }
        }
    }
}

void sendFrame(int sockfd, struct sockaddr_in *servaddr, SimulatedFrame *frame, uint8_t frameType, const char* payload) {
    memset(frame, 0, sizeof(SimulatedFrame));
    frame->type = frameType;
    if (payload != NULL && strlen(payload) > 0) {
        strncpy(frame->payload, payload, MAX_PAYLOAD_SIZE - 1);
        frame->payload[MAX_PAYLOAD_SIZE - 1] = '\0'; // Ensure null-termination
        frame->fcs = getCheckSumValue(frame, sizeof(SimulatedFrame) - sizeof(frame->fcs) + strlen(frame->payload));
    } else {
        frame->fcs = getCheckSumValue(frame, sizeof(SimulatedFrame) - sizeof(frame->fcs));
    }
    sendto(sockfd, frame, sizeof(*frame), 0, (const struct sockaddr*)servaddr, sizeof(*servaddr));
    printf("Frame type %d sent.\n", frameType);
}


int waitForResponse(int sockfd, const char *expectedResponse) {
    char buffer[BUFFER_SIZE];
    struct sockaddr_in fromAddr;
    socklen_t fromAddrLen = sizeof(fromAddr);
    int n = recvfrom(sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr*)&fromAddr, &fromAddrLen);
    if (n > 0) {
        buffer[n] = '\0';
        printf("Access Point: %s\n", buffer);
        return strcmp(buffer, expectedResponse) == 0;
    }
    return 0; // Incorrect response or error
}

void sendIncorrectChecksumFrame(int sockfd, struct sockaddr_in *servaddr) {
    SimulatedFrame incorrectFrame;
    memset(&incorrectFrame, 0, sizeof(SimulatedFrame));
    incorrectFrame.type = 0x10; // Arbitrary type for this example
    strcpy(incorrectFrame.address1, "AABBCCDDEEFF");
    strcpy(incorrectFrame.address2, "FFEEDDCCBBAA");
    strcpy(incorrectFrame.address3, "AABBCCDDEEFF");
    incorrectFrame.duration_id = htons(1);
    // Intentionally incorrect checksum for simulation
    incorrectFrame.fcs = 0xDEADBEEF; // Deliberately incorrect
    
    sendto(sockfd, &incorrectFrame, sizeof(incorrectFrame), 0, (const struct sockaddr*)servaddr, sizeof(*servaddr));
    printf("Frame with incorrect FCS sent.\n");
}

void testScenario(int sockfd, struct sockaddr_in *servaddr) {
    // Send a valid frame
    SimulatedFrame validFrame;
    sendFrame(sockfd, servaddr, &validFrame, 0x10, "Valid payload");

    // Wait for ACK
    if (!waitForAckWithTimeout(sockfd, ACK_WAIT_TIME)) {
        printf("Failed to receive ACK for the valid frame.\n");
    } else {
        printf("ACK received for the valid Frame 1.\n");
    }

    // Send four frames with errors
    for (int i = 1; i <= 4; i++) {
        printf("Sending frame %d with an error.\n", i + 1);
        sendIncorrectChecksumFrame(sockfd, servaddr); // Modify this function as needed to introduce different errors
        sleep(1); // Wait a bit before sending the next frame
    }
}

void sendWrongFormatFrame(int sockfd, struct sockaddr_in *servaddr) {
    SimulatedFrame wrongFormatFrame;
    memset(&wrongFormatFrame, 0, sizeof(SimulatedFrame));

    // Setting up a frame type that is considered invalid or unexpected.
    wrongFormatFrame.type = 0xFF; // Assuming 0xFF is not a valid frame type in your protocol

    // You can also fill other fields in a way that deviates from the expected format, if necessary.

    // Note: We're not focusing on the checksum here, assuming that the format error is our primary concern.
    // However, you can still calculate a correct checksum for this "incorrectly formatted" frame if needed.

    sendto(sockfd, &wrongFormatFrame, sizeof(wrongFormatFrame), 0, (const struct sockaddr*)servaddr, sizeof(*servaddr));
    printf("Frame with wrong format sent.\n");
}

void testScenario2(int sockfd, struct sockaddr_in *servaddr) {
    

     // Send a valid frame
    SimulatedFrame validFrame;
    sendFrame(sockfd, servaddr, &validFrame, 0x10, "Valid payload");

    // Wait for ACK
    if (!waitForAckWithTimeout(sockfd, ACK_WAIT_TIME)) {
        printf("Failed to receive ACK for the valid frame.\n");
    } else {
        printf("ACK received for the valid Frame 1.\n");
    }


    // Now send frames with wrong formats
    for (int i = 1; i <= 4; i++) {
        printf("Sending frame %d with a wrong format.\n", i + 1);
        sendWrongFormatFrame(sockfd, servaddr);
        // Optionally, wait for a response or a timeout
    }
}

void testScenario3(int sockfd, struct sockaddr_in *servaddr) {
    // Frame 1: Send a valid frame and receive ACK
    SimulatedFrame frame;
    printf("Sending Frame 1 with valid format.\n");
    sendFrame(sockfd, servaddr, &frame, 0x10, "Valid payload for Frame 1");
    if (waitForAckWithTimeout(sockfd, ACK_WAIT_TIME)) {
        printf("ACK received for Frame 1.\n");
    } else {
        printf("Failed to receive ACK for Frame 1.\n");
    }

    sleep(1); // Short delay between frames

    // Frame 2: Attempt to send and not receive ACK, retry 3 times
    printf("Sending Frame 2 with a retry mechanism.\n");
    int retryCount = 0;
    while (retryCount < 3) {
        sendFrame(sockfd, servaddr, &frame, 0x10, "Payload for Frame 2");
        sleep(1); // Assume ACK will not be received, simulate delay for retry
        printf("Retry %d for Frame 2...\n", retryCount + 1);
        retryCount++;
    }
    printf("No ACK received for Frame 2 after 3 retries from AP\n");

    // Frames 3-5: Send and receive ACKs without retrying
    for (int i = 3; i <= 5; i++) {
        char payload[50];
        snprintf(payload, sizeof(payload), "Payload for Frame %d", i);
        printf("Sending Frame %d with valid format.\n", i);
        sendFrame(sockfd, servaddr, &frame, 0x10, payload);
        if (waitForAckWithTimeout(sockfd, ACK_WAIT_TIME)) {
            printf("ACK received for Frame %d.\n", i);
        } else {
            printf("Failed to receive ACK for Frame %d. This shouldn't happen according to the scenario.\n", i);
        }
        sleep(1); // Wait a bit before sending the next frame
    }
}


int main() {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in servaddr = {0};
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(SERVER_PORT);
    servaddr.sin_addr.s_addr = inet_addr(SERVER_IP);

    SimulatedFrame frame;

    printf("--------------------------\n\n");

    // Association Request
    sendFrame(sockfd, &servaddr, &frame, 0x00, ""); // Empty payload for association
    if (!waitForResponse(sockfd, "Association Response: Accepted")) {
        printf("Association failed.\n");
        close(sockfd);
        return -1;
    }
    printf("Association successful.\n");

    // Probe Request
    sendFrame(sockfd, &servaddr, &frame, 0x01, ""); // Empty payload for probe
    if (!waitForResponse(sockfd, "Probe Response: Accepted")) {
        printf("Probe failed.\n");
        close(sockfd);
        return -1;
    }
    printf("Probe successful.\n");

    // RTS
    sendFrame(sockfd, &servaddr, &frame, 0x02, ""); // RTS frame, empty payload
    if (!waitForResponse(sockfd, "CTS")) {
        printf("Failed to receive CTS.\n");
        close(sockfd);
        return -1;
    }
    printf("CTS received.\n");

    // Data Frame
    const char* dataPayload = "Hello, this is a data payload";
    sendFrame(sockfd, &servaddr, &frame, 0x10, dataPayload); // Data frame with payload
    if (!waitForResponse(sockfd, "ACK")) {
        printf("Data transmission failed.\n");
        close(sockfd);
        return -1;
    }
    printf("Data transmission successful.\n");

      printf("--------------------------\n\n");

    // Now send a frame with an incorrect checksum to simulate an error
    sendIncorrectChecksumFrame(sockfd, &servaddr);

     // Data transmission successful, now proceed with the new logic
    printf("Proceeding with additional frames transmission...\n");
    sendFramesWithRetries(sockfd, &servaddr);

     printf("--------------------------\n\n");


    // Perform the test scenario  one valid frame with correct FCS and 4 frames with incorrect FCS
    testScenario(sockfd, &servaddr);
    printf("--------------------------\n\n");

     // Perform the test scenario one valid frame and 4 wrong format frames 
    testScenario2(sockfd, &servaddr);

     printf("--------------------------\n");

     // Perform the test scenario  valid frame with ack and retries scenario
    // testScenario3(sockfd, &servaddr);

    close(sockfd);
    return 0;
}
