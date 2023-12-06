#include <iostream>
#include <fstream>
#include <string>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <sys/select.h>
#include <vector>
#include <algorithm>
#include <cassert>
#include <time.h>
#include <sys/time.h>
#include <math.h>
#include "PacketHeader.h"
#include "crc32.h"

#define MAX_PACKET_LEN 1472
#define MAX_BUFFER_LEN 2048

size_t assemble_packet(char *buffer, unsigned int type, unsigned int seqNum, unsigned int length, char *chunk) {
    size_t packet_header_len = sizeof(struct PacketHeader);
    assert(packet_header_len + length <= MAX_PACKET_LEN);

    struct PacketHeader packet_header = {type, seqNum, length, crc32(chunk, length)};

    memcpy(buffer, &packet_header, packet_header_len);
    memcpy(buffer + packet_header_len, chunk, length);

    return packet_header_len + length;
}

struct PacketHeader parse_packet_header(char *buffer) {
    struct PacketHeader packet_header;
    memcpy(&packet_header, buffer, sizeof(struct PacketHeader));
    return packet_header;
}

size_t fread_nth_chunk(char *chunk, int n, long file_len, FILE *fileptr) {
    size_t max_chunk_len = MAX_PACKET_LEN - sizeof(struct PacketHeader);
    long offset = max_chunk_len * n;
    assert(offset < file_len);

    size_t chunk_len = file_len - offset < max_chunk_len ? file_len - offset : max_chunk_len;
    long cur_offset = ftell(fileptr);
    fseek(fileptr, offset - cur_offset, SEEK_CUR);

    fread(chunk, chunk_len, 1, fileptr);
    return chunk_len;
}

void left_shift_array(int *array, int num_elements, int shift_by) {
    assert(num_elements > 0);
    assert(shift_by >= 0);
    assert(shift_by <= num_elements);

    if (shift_by == 0) {
        return;
    }

    memmove(&array[0], &array[shift_by], (num_elements - shift_by) * sizeof(int));

    for (int i = num_elements - shift_by; i < num_elements; i++) {
        array[i] = -1;
    }
}

int min(int a, int b) {
    return a < b ? a : b;
}

int main(int argc, char *argv[]) {
    if (argc < 6) {
        std::cout << "Error: Usage is ./wSender <input_file> <window_size> <log> <receiver_IP> <receiver_port>\n";
        return 1;
    }

    char *input_file = argv[1];
    int window_size = atoi(argv[2]);
    char *log = argv[3];
    char *receiver_IP = argv[4];
    int receiver_port = atoi(argv[5]);

    // Init log file pointer
    FILE *log_fileptr = fopen(log, "a+");

    // Init UDP sender
    int sockfd;
    struct sockaddr_in recv_addr;
    struct sockaddr_in ACK_addr;
    int addr_len = sizeof(struct sockaddr);
    struct hostent *he;
    int numbytes;
    char buffer[MAX_PACKET_LEN];
    bzero(buffer, MAX_PACKET_LEN);
    char ACK_buffer[MAX_PACKET_LEN];
    bzero(ACK_buffer, MAX_PACKET_LEN);

    if ((he = gethostbyname(receiver_IP)) == NULL) {
        perror("gethostbyname");
        exit(1);
    }

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
        perror("socket");
        exit(1);
    }

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 500000;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        perror("Error");
    }

    recv_addr.sin_family = AF_INET;
    recv_addr.sin_port = htons(receiver_port);
    recv_addr.sin_addr = *((struct in_addr *) he->h_addr);
    memset(&(recv_addr.sin_zero), '\0', 8);

    // Init file pointer
    FILE *fileptr;
    long file_len;
    char chunk[MAX_PACKET_LEN];
    bzero(chunk, MAX_PACKET_LEN);

    fileptr = fopen(input_file, "rb");
    fseek(fileptr, 0, SEEK_END);
    file_len = ftell(fileptr);
    rewind(fileptr);

    size_t packet_len;
    size_t chunk_len;

    // Repeat sending START until ACK
    srand(time(NULL));
    unsigned int rand_num = rand();
    bzero(buffer, MAX_PACKET_LEN);
    bzero(chunk, MAX_PACKET_LEN);
    packet_len = assemble_packet(buffer, 0, rand_num, 0, chunk);

    while (true) {
        if ((numbytes = sendto(sockfd, buffer, packet_len, 0,
                               (struct sockaddr *) &recv_addr, sizeof(struct sockaddr))) == -1) {
            perror("sendto");
            exit(1);
        }

        struct PacketHeader packet_header = parse_packet_header(buffer);
        fprintf(log_fileptr, "%u %u %u %u\n", packet_header.type, packet_header.seqNum, packet_header.length,
                packet_header.checksum);
        fflush(log_fileptr);

        if ((numbytes = recvfrom(sockfd, ACK_buffer, MAX_BUFFER_LEN - 1, 0,
                                 (struct sockaddr *) &ACK_addr, (socklen_t *) &addr_len)) == -1) {
            continue;
        }

        struct PacketHeader ack_packet_header = parse_packet_header(ACK_buffer);
        fprintf(log_fileptr, "%u %u %u %u\n", ack_packet_header.type, ack_packet_header.seqNum,
                ack_packet_header.length,
                ack_packet_header.checksum);
        fflush(log_fileptr);

        if (ack_packet_header.type == 3 && ack_packet_header.seqNum == rand_num) {
            break;
        }
    }

    // Sending chunks
    int num_chunks = (int) ceil((double) file_len / (double) (MAX_PACKET_LEN - sizeof(struct PacketHeader)));
    int status[window_size]; // -1: not sent, 0: sent not acked, 1: acked
    for (int i = 0; i < window_size; i++) {
        status[i] = -1;
    }

    int window_start = 0;
    bool resend_all = true;
    struct timeval start_time;
    while (window_start != num_chunks) {
        for (int i = 0; i < min(window_size, num_chunks - window_start); i++) {
            if (status[i] == -1 || (resend_all && status[i] == 0)) {
                chunk_len = fread_nth_chunk(chunk, i + window_start, file_len, fileptr);
                packet_len = assemble_packet(buffer, 2, i + window_start, chunk_len, chunk);
                if ((numbytes = sendto(sockfd, buffer, packet_len, 0,
                                       (struct sockaddr *) &recv_addr, sizeof(struct sockaddr))) == -1) {
                    perror("sendto");
                    exit(1);
                }
                status[i] = 0;

                struct PacketHeader packet_header = parse_packet_header(buffer);
                fprintf(log_fileptr, "%u %u %u %u\n", packet_header.type, packet_header.seqNum, packet_header.length,
                        packet_header.checksum);
                fflush(log_fileptr);
            }
        }

        if (resend_all) {
            gettimeofday(&start_time, NULL);
        }

        if ((numbytes = recvfrom(sockfd, ACK_buffer, MAX_BUFFER_LEN - 1, 0,
                                 (struct sockaddr *) &ACK_addr, (socklen_t *) &addr_len)) == -1) {
            resend_all = true;
            continue;
        }

        resend_all = false;

        struct PacketHeader ack_packet_header = parse_packet_header(ACK_buffer);
        fprintf(log_fileptr, "%u %u %u %u\n", ack_packet_header.type, ack_packet_header.seqNum,
                ack_packet_header.length,
                ack_packet_header.checksum);
        fflush(log_fileptr);

        if (ack_packet_header.type == 3 && ack_packet_header.seqNum > window_start) {
            left_shift_array(status, window_size, ack_packet_header.seqNum - window_start);
            window_start = ack_packet_header.seqNum;
            // reset the timer
            gettimeofday(&start_time, NULL);
            continue;
        }
        
        struct timeval cur_time;
        gettimeofday(&cur_time, NULL);
        double duration = (cur_time.tv_sec - start_time.tv_sec) * 1000.0 +
                            (cur_time.tv_usec - start_time.tv_usec) / 1000.0;
        if (duration > 500) {
            resend_all = true;
        }
    }


    // Repeat sending CLOSE until ACK
    bzero(buffer, MAX_PACKET_LEN);
    bzero(chunk, MAX_PACKET_LEN);
    packet_len = assemble_packet(buffer, 1, rand_num, 0, chunk);

    while (true) {
        if ((numbytes = sendto(sockfd, buffer, packet_len, 0,
                               (struct sockaddr *) &recv_addr, sizeof(struct sockaddr))) == -1) {
            perror("sendto");
            exit(1);
        }

        struct PacketHeader packet_header = parse_packet_header(buffer);
        fprintf(log_fileptr, "%u %u %u %u\n", packet_header.type, packet_header.seqNum, packet_header.length,
                packet_header.checksum);
        fflush(log_fileptr);

        if ((numbytes = recvfrom(sockfd, ACK_buffer, MAX_BUFFER_LEN - 1, 0,
                                 (struct sockaddr *) &ACK_addr, (socklen_t *) &addr_len)) == -1) {
            continue;
        }

        struct PacketHeader ack_packet_header = parse_packet_header(ACK_buffer);
        fprintf(log_fileptr, "%u %u %u %u\n", ack_packet_header.type, ack_packet_header.seqNum,
                ack_packet_header.length,
                ack_packet_header.checksum);
        fflush(log_fileptr);

        if (ack_packet_header.type == 3 && ack_packet_header.seqNum == rand_num) {
            break;
        }
    }

    close(sockfd);
    fclose(fileptr);
    fclose(log_fileptr);

    return 0;
}
