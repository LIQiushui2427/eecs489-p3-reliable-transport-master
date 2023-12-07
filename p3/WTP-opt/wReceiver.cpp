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
#define MAX_FILENAME_LEN 100
#define START 0
#define END 1
#define DATA 2
#define ACK 3
bool createDirectory(const char *path) {
    int status = mkdir(path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    if (status == 0 || errno == EEXIST) {
        return true;
    } else {
        std::cerr << "Error creating directory: " << path << std::endl;
        perror("mkdir");
        return false;
    }
}
FILE* openFileForReadWrite(const char* filename) {
    printf("Opening file %s\n", filename);
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

    struct PacketHeader packet_header = generatePacketHeader(type, seqNum, length, chunk);

    copyPacketData(buffer, &packet_header, chunk);

    return packet_header_len + length;
}
struct PacketHeader getHeaderFromPacket(char *buffer) {
    struct PacketHeader packet_header;
    char *header_ptr = (char *)&packet_header;
    size_t header_size = sizeof(struct PacketHeader);

    for (size_t i = 0; i < header_size; ++i) {
        header_ptr[i] = buffer[i];
    }

    return packet_header;
}

size_t parse_chunk(char *buffer, char *chunk) {
    struct PacketHeader packet_header = getHeaderFromPacket(buffer);
    size_t packet_len = packet_header.length;

    for (size_t i = 0; i < packet_len; ++i) {
        chunk[i] = buffer[sizeof(struct PacketHeader) + i];
    }

    return packet_len;
}


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

    // Manual shift using loops
    for (int i = 0; i < num_elements - shift_by; ++i) {
        array[i] = array[i + shift_by];
    }

    // Set the remaining elements to 0
    for (int i = num_elements - shift_by; i < num_elements; ++i) {
        array[i] = 0;
    }
}

void writeLog(struct PacketHeader h,FILE *f){
    fprintf(f, "%u %u %u %u\n", h.type,h.seqNum,h.length,h.checksum);fflush(f);
}

int main(int argc, char *argv[]) {
    if (argc < 5) {
        std::cout << "Error: Usage is ./wReceiver <port_num> <log> <window_size> <file_dir>\n";
        return 1;
    }

    int port_num = atoi(argv[1]);
    char *log = argv[4];
    int window_size = atoi(argv[2]);
    char *file_dir = argv[3];

    // if not exist, create file_dir
    if (access(file_dir, F_OK) == -1) {
        printf("Creating directory %s\n", file_dir);
        mkdir(file_dir, 0700);
    }
    // Init log file pointer
    printf("Log file: %s\n", log);
    std::string logFilePath(log);
    size_t lastSlash = logFilePath.find_last_of('/');
    std::string directory = logFilePath.substr(0, lastSlash);
    std::string fileName = logFilePath.substr(lastSlash + 1);

    printf("Directory: %s\n", directory.c_str());
    printf("Filename: %s\n", fileName.c_str());
    if (!createDirectory(directory.c_str())) {
        return 1;
    }
    std::string logFileFullPath = directory + "/" + fileName;
    FILE *log_fileptr = fopen(log, "a+");

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
    int window_s = 0;
    int status[window_size]; // 0: not received, 1: received and acked

    while (true) {
        // Init filename
        FILE *fileptr = nullptr;
        char chunk[MAX_PACKET_LEN];
        memset(chunk, 0, MAX_PACKET_LEN);
        char output_file[strlen(file_dir) + 15];
        sprintf(output_file, "%s/FILE-%d.out", file_dir, file_num++);

        int rand_num = -1; // END seqNum should be the same as START;

        // Reset receive window
        for (int i = 0; i < window_size; i++) {
            status[i] = 0;
        }
        window_s = 0;

        bool completed = false;
        while (!completed) {
            if ((numbytes = recvfrom(sockfd, buffer, MAX_BUFFER_LEN - 1, 0,
                                     (struct sockaddr *) &send_addr, (socklen_t *) &addr_len)) == -1) {
                perror("recv");
                exit(1);
            }

            char *send_ip = inet_ntoa(send_addr.sin_addr);
            int send_port = ntohs(send_addr.sin_port);

            struct PacketHeader packet_header = getHeaderFromPacket(buffer);
            writeLog(packet_header,log_fileptr);

            bzero(chunk, MAX_PACKET_LEN);
            size_t chunk_len = parse_chunk(buffer, chunk);

            if (crc32(chunk, chunk_len) != packet_header.checksum) {
                printf("Checksum incorrect\n");
                continue;
            }

            int seqNum = -1;
            bool should_continue = false;
            if (packet_header.type == 0) {
                    if (rand_num != -1 && rand_num != packet_header.seqNum) {
                        printf("Duplicate START\n");
                        should_continue = true;
                    } else {
                        rand_num = packet_header.seqNum;
                        seqNum = packet_header.seqNum;
                        if (fileptr == nullptr) {
                            fileptr = openFileForReadWrite(output_file);
                        }
                    }
            }
                else if (packet_header.type == 1) {
                    if (rand_num != packet_header.seqNum && rand_num != -1) {
                        printf("END seqNum not same as START\n");
                        should_continue = true;
                    } else {
                        seqNum = packet_header.seqNum;
                        completed = true;
                        rand_num = -1;
                    }
                }
                else if (packet_header.type == 2) {
                    if (rand_num == -1) {
                        printf("No START received\n");
                        should_continue = true;
                    } else {
                        if (packet_header.seqNum < window_s) {
                            seqNum = packet_header.seqNum;
                        } else if (packet_header.seqNum > window_s) {
                            if (packet_header.seqNum < window_s + window_size) {
                                seqNum = packet_header.seqNum;
                                if (status[packet_header.seqNum - window_s] == 0) {
                                    status[packet_header.seqNum - window_s] = 1;
                                    writeNthChunkToFile(chunk, packet_header.seqNum, chunk_len, fileptr);
                                }
                            } else {
                                should_continue = true;
                            }
                        } else {
                            // packet_header.seqNum == window_s
                            seqNum = packet_header.seqNum;
                            if (status[0] == 0) {
                                status[0] = 1;
                                writeNthChunkToFile(chunk, packet_header.seqNum, chunk_len, fileptr);
                            }

                            int shift_by = 0;
                            for (int i = 0; i < window_size; i++) {
                                if (status[i] == 1) {
                                    shift_by++;
                                } else {
                                    break;
                                }
                            }
                            window_s += shift_by;
                            move_window(status, window_size, shift_by);
                        }
                    }
                } else {
                    should_continue = true;
                }

            if (should_continue) {
                continue;
            }

            // Init ACK sender
            struct sockaddr_in ACK_addr;
            struct hostent *he;

            if ((he = gethostbyname(send_ip)) == NULL) {
                perror("gethostbyname");
                exit(1);
            }

            ACK_addr.sin_family = AF_INET;
            ACK_addr.sin_port = htons(send_port);
            ACK_addr.sin_addr = *((struct in_addr *) he->h_addr);
            memset(&(ACK_addr.sin_zero), '\0', 8);

            char ACK_buffer[MAX_BUFFER_LEN];
            memset(ACK_buffer, 0, MAX_BUFFER_LEN);

            char empty_chunk[1];
            memset(empty_chunk, 0, 1);
            assert(seqNum >= 0);
            size_t ACK_packet_len = createAndFillPacket(ACK_buffer, 3, seqNum, 0, empty_chunk);


                if ((numbytes = sendto(sockfd, ACK_buffer, ACK_packet_len, 0,
                                       (struct sockaddr *) &ACK_addr, sizeof(struct sockaddr))) == -1) {
                    perror("sendto");
                    exit(1);
                }


            writeLog(getHeaderFromPacket(ACK_buffer),log_fileptr);
        }

        fclose(fileptr);
    }

    close(sockfd);
    fclose(log_fileptr);

    return 0;
}
