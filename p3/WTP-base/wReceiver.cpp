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
#include <sys/stat.h>
#include <sys/types.h>
#include <filesystem>

#define MAX_PACKET_LEN 1472
#define MAX_BUFFER_LEN 2048
#define START 0
#define END 1
#define DATA 2
#define ACK 3

/*Referred to EECS489 p3 in github. We added our understanding and made our own implementation*/
/*https://github.com/zianke/eecs489-p3-reliable-transport*/
FILE* open_file(char *path,char *state){
    int len = strlen(path);
    for(int i=0;i<len;i++){
        if(path[i]=='/'){
            path[i]=0;
            if (access(path, F_OK) != 0) mkdir(path, 0777);
            path[i]='/';
        }
    }
    FILE *f_log=fopen(path, state);
    return f_log;
}
FILE* openFileForReadWrite(const char* filename) {
    FILE* fileptr = fopen(filename, "rb+");
    if (fileptr == nullptr) {
        // File doesn't exist, create and open for reading and writing
        fileptr = fopen(filename, "wb+");
    }
    return fileptr;
}

struct PacketHeader generatePacketHeader(unsigned int type, unsigned int seqNum, unsigned int length, char *chunk) {
    return {type, seqNum, length, crc32(chunk, length)};
}

void copyPacketData(char *buffer, const struct PacketHeader *header, const char *chunk) {
    size_t packet_header_len = sizeof(struct PacketHeader);
    memcpy(buffer, header, packet_header_len);
    memcpy(buffer + packet_header_len, chunk, header->length);
}
size_t createAndFillPacket(char *buffer, unsigned int type, unsigned int seqNum, unsigned int length, char *chunk) {
    size_t packet_header_len = sizeof(struct PacketHeader);
    assert(packet_header_len + length <= MAX_PACKET_LEN);

    struct PacketHeader header = generatePacketHeader(type, seqNum, length, chunk);

    copyPacketData(buffer, &header, chunk);

    return packet_header_len + length;
}
struct PacketHeader parse_packet_header(char *buffer) {
    struct PacketHeader header;
    memcpy(&header, buffer, sizeof(struct PacketHeader));
    return header;
}

size_t parse_chunk(char *buffer, char *chunk) {
    struct PacketHeader header = parse_packet_header(buffer);
    size_t packet_len = header.length;
    memcpy(chunk, buffer + sizeof(struct PacketHeader), packet_len);
    return packet_len;
}

#include <stdio.h>

size_t writeNthChunkToFile(char *dataChunk, int chunkNumber, size_t chunkSize, FILE *outputFile) {
    size_t maxChunkSize = MAX_PACKET_LEN - sizeof(struct PacketHeader);
    long offset = maxChunkSize * chunkNumber;

    // Check if fseek was successful
    if (fseek(outputFile, offset, SEEK_SET) == 0) {
        // Write the dataChunk directly without using fwrite for offset
        size_t bytesWritten = fwrite(dataChunk, chunkSize, 1, outputFile);
        
        // Check if fwrite was successful
        if (bytesWritten == 1) {
            return chunkSize;
        } else {
            // Handle fwrite error
            // For example, you may print an error message or return an error code
            fprintf(stderr, "Error writing dataChunk to file\n");
            return 0; // Indicate failure
        }
    } else {
        // Handle fseek error
        // For example, you may print an error message or return an error code
        fprintf(stderr, "Error seeking to offset in file\n");
        return 0; // Indicate failure
    }
}




void move_window(int *array, int num_elements, int shift_by) {
    assert(num_elements > 0);
    assert(shift_by > 0);
    assert(shift_by <= num_elements);

    memmove(&array[0], &array[shift_by], (num_elements - shift_by) * sizeof(int));

    for (int i = num_elements - shift_by; i < num_elements; i++) {
        array[i] = 0;
    }
}

void writeLog(struct PacketHeader h,FILE *f){
    std::cout << h.type << " " << h.seqNum << " " << h.length << " " << h.checksum << std::endl;
    fprintf(f, "%u %u %u %u\n", h.type,h.seqNum,h.length,h.checksum);fflush(f);
}

int main(int argc, char *argv[]) {
    if (argc < 5) {
        std::cerr << "Error: Usage is ./wReceiver <port_num> <log> <window_length> <file_dir>\n";
        return 1;
    }

    // Parse command line arguments
    int port_num = std::atoi(argv[1]);
    char *log = argv[4];
    int window_length = std::atoi(argv[2]);
    char *file_dir = argv[3];

    // if not exist, create file_dir
    // if not exist, create file_dir
    if (access(file_dir, F_OK) == -1) {

        printf("Creating directory %s\n", file_dir);
        // if there are multiple levels of directories, create them
        std::filesystem::create_directories(file_dir);
    }

    //initialize log file
    FILE *log_fileptr = open_file(log,"a+");

    // Initialize UDP receiver
    int sockfd;
    struct sockaddr_in recv_addr;
    struct sockaddr_in send_addr;
    int addr_len = sizeof(struct sockaddr);
    int numbytes;
    char buffer[MAX_BUFFER_LEN];
    memset(buffer, 0, MAX_BUFFER_LEN);

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
        perror("socket");
        std::fclose(log_fileptr);
        return 1;
    }

    recv_addr.sin_family = AF_INET;
    recv_addr.sin_port = htons(port_num);
    recv_addr.sin_addr.s_addr = INADDR_ANY;
    memset(&(recv_addr.sin_zero), '\0', 8);

    if (bind(sockfd, reinterpret_cast<struct sockaddr *>(&recv_addr), sizeof(struct sockaddr)) == -1) {
        perror("bind");
        std::fclose(log_fileptr);
        close(sockfd);
        return 1;
    }

    int file_num = 0;
    int window_s = 0;
    int windowStatusArr[window_length]; // 0: not 
    while (true) {
        FILE *fileptr = nullptr;
        char chunk[MAX_PACKET_LEN];
        bzero(chunk, MAX_PACKET_LEN);
        char output_file[strlen(file_dir) + 15];
        sprintf(output_file, "%s/FILE-%d.out", file_dir, file_num++);

        int rand_num = -1;

        // Reset receive window
        for (int i = 0; i < window_length; i++) {
            windowStatusArr[i] = 0;
        }
        window_s = 0;

        bool completed = false;
        printf("Waiting for packets...\n");
        while (!completed) {
            if ((numbytes = recvfrom(sockfd, buffer, MAX_BUFFER_LEN - 1, 0,
                                     (struct sockaddr *) &send_addr, (socklen_t *) &addr_len)) == -1) {
                perror("recv");
                exit(1);
            }
            printf("Received packet from %s:%d\n", inet_ntoa(send_addr.sin_addr), ntohs(send_addr.sin_port));


            char *send_ip = inet_ntoa(send_addr.sin_addr);
            int send_port = ntohs(send_addr.sin_port);

            struct PacketHeader header = parse_packet_header(buffer);
            writeLog(header,log_fileptr);

            memset(chunk, 0, MAX_PACKET_LEN);
            size_t chunk_len = parse_chunk(buffer, chunk);

            if (crc32(chunk, chunk_len) != header.checksum) {
                printf("Checksum incorrect! Crc32: %u, checksum: %u\n", crc32(chunk, chunk_len), header.checksum);
                continue;
            }

            int seqNum = -1;
            bool cont = false;
            if (header.type == START) {
                if (rand_num != -1 && rand_num != header.seqNum) {
                    printf("Duplicate START\n");
                    cont = true;
                } else {
                    rand_num = header.seqNum;
                    seqNum = header.seqNum;
                    if (fileptr == nullptr) {
                        fileptr = openFileForReadWrite(output_file);
                    }
                }
            } else if (header.type == END) {
                if (rand_num != header.seqNum && rand_num != -1) {
                    printf("END seqNum not same as START\n");
                    cont = true;
                } else {
                    seqNum = header.seqNum;
                    completed = true;
                    rand_num = -1;
                }
            } else if (header.type == DATA) {
                if (rand_num == -1) {
                    printf("No START received\n");
                    cont = true;
                } else {
                    seqNum = window_s;
                    if (header.seqNum == seqNum) {
                        // header.seqNum == window_s
                        if (windowStatusArr[0] == 0) {
                            windowStatusArr[0] = 1;
                            writeNthChunkToFile(chunk, header.seqNum, chunk_len, fileptr);
                        }

                        int shift_by = 0;
                        for (int i = 0; i < window_length; i++) {
                            if (windowStatusArr[i] == 1) {
                                shift_by++;
                            } else {
                                break;
                            }
                        }
                        window_s += shift_by;
                        seqNum = window_s;
                        move_window(windowStatusArr, window_length, shift_by);
                    }
                }
            } else {
                cont = true;
            }


            if (cont) {
                continue;
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
            memset(ACK_buffer, 0, MAX_BUFFER_LEN);

            char empty_chunk[1];
            memset(empty_chunk, 0, 1);

            assert(seqNum >= 0);
            size_t ACK_packet_len = createAndFillPacket(ACK_buffer, 3, seqNum, 0, empty_chunk);

            if ((numbytes = sendto(sockfd, ACK_buffer, ACK_packet_len, 0,
                                   (struct sockaddr *) &ACK_addr, sizeof(struct sockaddr))) == -1) {
                std::cerr << "Error sending ACK packet\n";
                exit(1);
            }

            writeLog(parse_packet_header(ACK_buffer),log_fileptr);
        }
        printf("Completed file %s\n", output_file);
        fclose(fileptr);
    }
    printf("Closing socket\n");
    close(sockfd);
    fclose(log_fileptr);

    return 0;
}
