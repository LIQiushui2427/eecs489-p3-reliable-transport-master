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


/*Referred to EECS489 p3 in github. We added our understanding and made our own implementation*/
/*https://github.com/zianke/eecs489-p3-reliable-transport*/
FILE* open_file(char *path,char *state){
    assert(path != nullptr);
    assert(state != nullptr);
    int len = strlen(path);
    for(int i=0;i<len;i++){
        if(path[i]=='/'){
            path[i]=0;
            if (access(path, F_OK) != 0) mkdir(path, 0777);
            path[i]='/';
        }
    }
    FILE *f_log=fopen(path, state);
    if(f_log==nullptr){
        std::cerr << "Error opening file: " << path << std::endl;
        perror("fopen");
        exit(1);
    }
    return f_log;
}
bool createDirectory(const char *path) {
    assert(path != nullptr);
    int windowStatusArr = mkdir(path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    if (windowStatusArr == 0 || errno == EEXIST) {
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

struct PacketHeader generatePacketHeader(unsigned int type, unsigned int seqNum, unsigned int length, char *dataChunk) {
    return {type, seqNum, length, crc32(dataChunk, length)};
}

void copyPacketData(char *buffer, const struct PacketHeader *header, const char *dataChunk) {
    assert(sizeof(struct PacketHeader) <= MAX_PACKET_LEN);
    size_t packet_header_len = sizeof(struct PacketHeader);
    memcpy(buffer, header, packet_header_len);
    memcpy(buffer + packet_header_len, dataChunk, header->length);
}
size_t createAndFillPacket(char *buffer, unsigned int type, unsigned int seqNum, unsigned int length, char *dataChunk) {
    size_t packet_header_len = sizeof(struct PacketHeader);
    assert(packet_header_len + length <= MAX_PACKET_LEN);

    struct PacketHeader header = generatePacketHeader(type, seqNum, length, dataChunk);

    copyPacketData(buffer, &header, dataChunk);

    return packet_header_len + length;
}
struct PacketHeader getHeaderFromPacket(char *buffer) {
    assert(sizeof(struct PacketHeader) <= MAX_PACKET_LEN);
    assert(sizeof(struct PacketHeader) <= MAX_BUFFER_LEN);
    struct PacketHeader header;
    char *header_ptr = (char *)&header;
    size_t header_size = sizeof(struct PacketHeader);

    for (size_t i = 0; i < header_size; ++i) {
        header_ptr[i] = buffer[i];
    }

    return header;
}

size_t parse_chunk(char *buffer, char *dataChunk) {
    struct PacketHeader header = getHeaderFromPacket(buffer);
    size_t packet_len = header.length;

    for (size_t i = 0; i < packet_len; ++i) {
        dataChunk[i] = buffer[sizeof(struct PacketHeader) + i];
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




void move_window(int *array, int num_elements, int step) {
    assert(num_elements > 0);
    assert(step > 0);
    assert(step <= num_elements);

    // Manual shift using loops
    for (int i = 0; i < num_elements - step; ++i) {
        array[i] = array[i + step];
    }

    // Set the remaining elements to 0
    for (int i = num_elements - step; i < num_elements; ++i) {
        array[i] = 0;
    }
}
void commit_step(bool *cont, int seqNum, int window_s, int window_length, int *windowStatusArr) {
    // if cont is false, then change it to true, the cont should be change outside the function
    if (!*cont) {
        *cont = true;
    }
    std::cout << "Commit step\n";
    std::cout << "seqNum: " << seqNum << std::endl;
    std::cout << "window_s: " << window_s << std::endl;
}
void writeLog(struct PacketHeader h,FILE *f){
    std::cout << h.type << " " << h.seqNum << " " << h.length << " " << h.checksum << std::endl;
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
        std::filesystem::create_directory(file_dir);
    }
    
    //initialize log file
    FILE *log_fileptr = open_file(log,"a+");

    // Init UDP receiver
    int sockfd;
    struct sockaddr_in recv_addr;
    struct sockaddr_in send_addr;
    int addr_len = sizeof(struct sockaddr);
    int numbytes;
    char buffer[MAX_BUFFER_LEN];
    memset(buffer, 0, MAX_BUFFER_LEN);

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
        perror("socket");
        exit(1);
    }

    recv_addr.sin_family = AF_INET;
    recv_addr.sin_port = htons(port_num);
    recv_addr.sin_addr.s_addr = INADDR_ANY;
    memset(&(recv_addr.sin_zero), '\0', 8);

    if (bind(sockfd, (struct sockaddr *) &recv_addr, sizeof(struct sockaddr)) == -1) {
        std::cerr << "Error binding to port " << port_num << std::endl;
        exit(1);
    }

    int file_num = 0;
    int window_s = 0;
    int windowStatusArr[window_size]; // 0: not received, 1: received and acked

    while (true) {
        // Init filename
        FILE *fileptr = nullptr;
        char dataChunk[MAX_PACKET_LEN];
        memset(dataChunk, 0, MAX_PACKET_LEN);
        char output_file[strlen(file_dir) + 15];
        sprintf(output_file, "%s/FILE-%d.out", file_dir, file_num++);

        int seqNumCursor = -1; // END seqNum should be the same as START;

        // Reset receive window
        for (int i = 0; i < window_size; i++) {
            windowStatusArr[i] = 0;
        }
        window_s = 0;

        bool completedFlag = false;
        while (!completedFlag) {
            // Receive packet
            if ((numbytes = recvfrom(sockfd, buffer, MAX_BUFFER_LEN - 1, 0,
                                     (struct sockaddr *) &send_addr, (socklen_t *) &addr_len)) == -1) {
                std::cerr << "Error receiving packet" << std::endl;
                exit(1);
            }

            char *send_ip = inet_ntoa(send_addr.sin_addr);
            int send_port = ntohs(send_addr.sin_port);

            struct PacketHeader header = getHeaderFromPacket(buffer);
            writeLog(header,log_fileptr);

            memset(dataChunk, 0, MAX_PACKET_LEN);
            size_t chunk_len = parse_chunk(buffer, dataChunk);

            if (crc32(dataChunk, chunk_len) != header.checksum) {
                std::cout << "Checksum incorrect\n";
                continue;
            }

            int seqNum = -1;
            bool cont = false;
            if (header.type == 0 && !(seqNumCursor != -1 && seqNumCursor != header.seqNum)){
                        seqNumCursor = header.seqNum;
                        seqNum = header.seqNum;
                        if (fileptr == nullptr) {
                            fileptr = openFileForReadWrite(output_file);
                        }
            }
                else if (header.type == 1) {
                    if (seqNumCursor != header.seqNum && seqNumCursor != -1) {
                        std::cout << "END seqNum incorrect\n";
                        commit_step(&cont, seqNum, window_s, window_size, windowStatusArr);
                    } else {
                        seqNum = header.seqNum;
                        completedFlag = true;
                        seqNumCursor = -1;
                    }
                }
                else if (header.type == 2) {
                    if (seqNumCursor == -1) {
                        std::cout << "START not received\n";
                        commit_step(&cont, seqNum, window_s, window_size, windowStatusArr);
                    } else {
                        if (header.seqNum < window_s) {
                            seqNum = header.seqNum;
                        } else if (header.seqNum > window_s) {
                            if (header.seqNum < window_s + window_size) {
                                seqNum = header.seqNum;
                                if (windowStatusArr[header.seqNum - window_s] == 0) {
                                    windowStatusArr[header.seqNum - window_s] = 1;
                                    writeNthChunkToFile(dataChunk, header.seqNum, chunk_len, fileptr);
                                }
                            } else {
                                std::cout << "seqNum out of window\n";
                                std::cout << "header.seqNum: " << header.seqNum << std::endl;
                                commit_step(&cont, seqNum, window_s, window_size, windowStatusArr);
                            }
                        } else {
                            // header.seqNum == window_s
                            seqNum = header.seqNum;
                            if (windowStatusArr[0] == 0) {
                                windowStatusArr[0] = 1;
                                writeNthChunkToFile(dataChunk, header.seqNum, chunk_len, fileptr);
                            }

                            int step = 0;
                            for (int i = 0; i < window_size; i++) {
                                if (windowStatusArr[i] == 1) {
                                    step++;
                                } else {
                                    break;
                                }
                            }
                            window_s += step;
                            move_window(windowStatusArr, window_size, step);
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
                    std::cerr << "Error sending ACK packet to " << send_ip << ":" << send_port << std::endl;
                    exit(1);
                }
            std::cout << "ACK sent to " << send_ip << ":" << send_port << std::endl;

            writeLog(getHeaderFromPacket(ACK_buffer),log_fileptr);
        }

        fclose(fileptr);
    }
    std::cout << "File transfer completedFlag" << std::endl;
    close(sockfd);
    fclose(log_fileptr);

    return 0;
}
