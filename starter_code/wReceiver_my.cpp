#include "crc32.h"
#include <assert.h>
#include <string.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>

#include <stdlib.h>
#include <stdbool.h>
#include "PacketHeader.h"
/* Referred to eecs489 pa1 for some functions and also used some of the code from the starter code */
#define MAX_PACKET_LEN 1024
#define MAX_BUFFER_LEN 1032

// Function to assemble a packet
size_t assemble_packet(char *buffer, unsigned int type, unsigned int seqNum, unsigned int length, char *chunk) {
    size_t packet_header_len = sizeof(struct PacketHeader);
    assert(packet_header_len + length <= MAX_PACKET_LEN);

    // Create a PacketHeader struct
    struct PacketHeader packet_header = {type, seqNum, length, crc32(chunk, length)};

    // Copy the PacketHeader to the buffer
    memcpy(buffer, &packet_header, packet_header_len);

    // Copy the chunk data to the buffer after the header
    memcpy(buffer + packet_header_len, chunk, length);

    // Return the total length of the packet
    return packet_header_len + length;
}

// Function to parse a packet header from a buffer
struct PacketHeader get_header_from_buffer(char *buffer) {
    struct PacketHeader packet_header;
    memcpy(&packet_header, buffer, sizeof(struct PacketHeader));
    return packet_header;
}

// Function to parse a chunk from a buffer
size_t parse_chunk(char *buffer, char *chunk) {
    struct PacketHeader packet_header = get_header_from_buffer(buffer);
    size_t packet_len = packet_header.length;
    memcpy(chunk, buffer + sizeof(struct PacketHeader), packet_len);
    return packet_len;
}

// Function to write the nth chunk to a file
size_t fwrite_nth_chunk(char *chunk, int n, size_t chunk_len, FILE *fileptr) {
    size_t max_chunk_len = MAX_PACKET_LEN - sizeof(struct PacketHeader);
    long offset = max_chunk_len * n;

    long cur_offset = ftell(fileptr);
    fseek(fileptr, offset - cur_offset, SEEK_CUR);

    // Write the chunk to the file
    fwrite(chunk, chunk_len, 1, fileptr);

    // Return the length of the written chunk
    return chunk_len;
}

// Function to write to log
void writeLog(struct PacketHeader h,FILE *f){
    fprintf(f, "%u %u %u %u\n", h.type,h.seqNum,h.length,h.checksum);fflush(f);
}

// Function to left shift an array
void move_window(int *array, int num_elements, int shift_by) {
    assert(num_elements > 0);
    assert(shift_by > 0);
    assert(shift_by <= num_elements);

    // Calculate the number of elements to be shifted
    int num_shifted_elements = num_elements - shift_by;

    // Use memmove to shift elements to the left
    memmove(array, &array[shift_by], num_shifted_elements * sizeof(int));

    // Fill the remaining elements with 0
    memset(&array[num_shifted_elements], 0, shift_by * sizeof(int));
}

int main(int argc, char *argv[]) {
if (argc < 5) {
        fprintf(stderr, "Error: Usage is %s <port_num> <window-size> <output-dir> <log>", argv[0]);
        return 1;
    }

    int port_num = atoi(argv[1]);
    char *log = argv[4];
    int window_size = atoi(argv[2]);
    char *file_dir = argv[3];

    // Init outputdir
    FILE *log_fileptr = fopen(log, "a+");
    if (log_fileptr == NULL) {
        perror("fopen");
        exit(1);
    }
    printf("Created log file %s\n", log);
    // Init UDP receiver
    int sockfd;
    struct sockaddr_in recv_addr;
    struct sockaddr_in send_addr;
    int addr_len = sizeof(struct sockaddr);
    int numbytes;
    char buffer[MAX_BUFFER_LEN];
    bzero(buffer, MAX_BUFFER_LEN);

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
        perror("socket");
        exit(1);
    }

    recv_addr.sin_family = AF_INET;
    recv_addr.sin_port = htons(port_num);
    recv_addr.sin_addr.s_addr = INADDR_ANY;
    memset(&(recv_addr.sin_zero), '\0', 8);

    if (bind(sockfd, (struct sockaddr *) &recv_addr,
             sizeof(struct sockaddr)) == -1) {
        perror("bind");
        exit(1);
    }

    int file_num = 0;
    int window_start = 0;
    int status[window_size]; // 0: not received, 1: received and acked
    printf("Sever listening on port %d\n", port_num);
    while (true) {
        // Init filename
        FILE *fileptr = NULL;
        char chunk[MAX_PACKET_LEN];
        bzero(chunk, MAX_PACKET_LEN);
        char output_file[strlen(file_dir) + 10];
        sprintf(output_file, "%s/FILE-%d.out", file_dir, file_num++);
        printf("Waiting for file %s\n", output_file);
        int rand_num = -1; // END seqNum should be the same as START;

        // Reset receive window
        for (int i = 0; i < window_size; i++) {
            status[i] = 0;
        }
        window_start = 0;

        bool completed = false;
        while (!completed) {
            if ((numbytes = recvfrom(sockfd, buffer, MAX_BUFFER_LEN - 1, 0,
                                     (struct sockaddr *) &send_addr, (socklen_t *) &addr_len)) == -1) {
                perror("recvfrom");
                exit(1);
            }
            
            char *send_ip = inet_ntoa(send_addr.sin_addr);
            int send_port = ntohs(send_addr.sin_port);
            printf("Received packet from %s:%d\n", send_ip, send_port);
            printf("Packet is %d bytes long\n", numbytes);
            printf("Packet contains \"%s\"\n", buffer);

            // Write to log
            struct PacketHeader packet_header = get_header_from_buffer(buffer);
            writeLog(packet_header,log_fileptr);

            bzero(chunk, MAX_PACKET_LEN);
            size_t chunk_len = parse_chunk(buffer, chunk);

            // Check checksum
            if (crc32(chunk, chunk_len) != packet_header.checksum) {
                printf("Checksum incorrect, crc32: %u, checksum: %u\n", crc32(chunk, chunk_len),
                       packet_header.checksum);
                continue;
            }

            int seqNum = -1;
            switch (packet_header.type) {
                case 0:
                // START
                    if (rand_num != -1) {
                        if (rand_num != packet_header.seqNum) {
                            printf("Duplicate START\n");
                            continue;
                        }
                    } else {
                        rand_num = packet_header.seqNum;
                        seqNum = packet_header.seqNum;
                        
                        // Flush file if it exists
                        if (fileptr == NULL) {
                            printf("Opening file %s\n", output_file);
                            fileptr = fopen(output_file, "wb+");
                            if (fileptr == NULL) {
                                perror("fopen");
                                exit(1);
                            }
                        }
                    }
                    break;

                case 1:
                    if (rand_num != packet_header.seqNum && rand_num != -1) {
                        printf("END seqNum not same as START\n");
                        continue;
                    } else {
                        seqNum = packet_header.seqNum;
                        completed = true;
                        rand_num = -1;
                    }
                    break;
                case 2:
                    if (rand_num == -1) {
                        printf("No START received\n");
                        continue;
                    } else {
                        if (packet_header.seqNum < window_start) {
                            seqNum = window_start;
                        } else if (packet_header.seqNum > window_start) {
                            seqNum = window_start;
                            if (packet_header.seqNum < window_start + window_size) {
                                if (status[packet_header.seqNum - window_start] == 0) {
                                    status[packet_header.seqNum - window_start] = 1;
                                    fwrite_nth_chunk(chunk, packet_header.seqNum, chunk_len, fileptr);
                                }
                            }
                        } else {
                            // packet_header.seqNum == window_start
                            if (status[0] == 0) {
                                status[0] = 1;
                                fwrite_nth_chunk(chunk, packet_header.seqNum, chunk_len, fileptr);
                            }

                            int shift_by = 0;
                            for (int i = 0; i < window_size; i++) {
                                if (status[i] == 1) {
                                    shift_by++;
                                } else {
                                    break;
                                }
                            }
                            window_start += shift_by;
                            seqNum = window_start;
                            move_window(status, window_size, shift_by);
                        }
                    }
                    break;
                default:
                    continue;
                    break;
            }

            // Init ACK sender
            struct sockaddr_in ACK_addr;
            struct hostent *otherHost;

            if ((otherHost = gethostbyname(send_ip)) == NULL) {
                perror("gethostbyname");
                exit(1);
            }

            ACK_addr.sin_family = AF_INET;
            ACK_addr.sin_port = htons(send_port);
            ACK_addr.sin_addr = *((struct in_addr *) otherHost->h_addr);
            memset(&(ACK_addr.sin_zero), '\0', 8);

            char ACK_buffer[MAX_BUFFER_LEN];
            bzero(ACK_buffer, MAX_BUFFER_LEN);

            char empty_chunk[1];
            bzero(ACK_buffer, 1);

            assert(seqNum >= 0);
            size_t ACK_packet_len = assemble_packet(ACK_buffer, 3, seqNum, 0, empty_chunk);

            if ((numbytes = sendto(sockfd, ACK_buffer, ACK_packet_len, 0,
                                   (struct sockaddr *) &ACK_addr, sizeof(struct sockaddr))) == -1) {
                perror("sendto");
                exit(1);
            }

            // Write to log
            writeLog(packet_header,log_fileptr);
        }

        fclose(fileptr);
    }

    close(sockfd);
    fclose(log_fileptr);

    return 0;
}