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

/*Referred to EECS489 p3 in github. We added our understanding and made our own implementation
We make the sample code run in the autograder to test the behaviour of autograder. */
/*https://github.com/zianke/eecs489-p3-reliable-transport*/
#include <fstream>

std::ifstream::pos_type filesize(const char* filename)
{
    std::ifstream in(filename, std::ifstream::ate | std::ifstream::binary);
    return in.tellg(); 
}
class FileWorker 
{
    public:
        int error;
        // daclare functions
        FileWorker();
        ~FileWorker();
        int getError();
        FILE* open_file(char *path,char *state);
        bool createDirectory(const char *path);
        FILE* openFileForReadWrite(const char* filename);
        
};
FileWorker::FileWorker(){
    error = 0;
};
FileWorker::~FileWorker(){
    error = 0;
};
int FileWorker::getError(){
    return error;
};
FILE* FileWorker::open_file(char *path,char *state){
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
bool FileWorker::createDirectory(const char *path) {
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
FILE* FileWorker::openFileForReadWrite(const char* filename) {
    printf("Opening file %s\n", filename);
    FILE* fileptr = fopen(filename, "rb+");
    if (fileptr == nullptr) {
        // File doesn't exist, create and open for reading and writing
        fileptr = fopen(filename, "wb+");
    }
    return fileptr;
}
class WTPHandler
{
    public:
        int error;
        // daclare functions
        WTPHandler();
        ~WTPHandler();
        int getError();
        struct PacketHeader generatePacketHeader(unsigned int type, unsigned int seqNum, unsigned int length, char *datapiece);
        void copyPacketData(char *tmpBuffer, const struct PacketHeader *header, const char *datapiece);
        size_t createAndFillPacket(char *tmpBuffer, unsigned int type, unsigned int seqNum, unsigned int length, char *datapiece);
        struct PacketHeader parse_packet_header(char *tmpBuffer);
        size_t parse_chunk(char *tmpBuffer, char *datapiece);
        size_t writeNthChunkToFile(char *dataChunk, int chunkNumber, size_t chunkSize, FILE *outputFile);
        void move_window(int *array, int num_elements, int step);
        void writeLog(struct PacketHeader h,FILE *f);
        void commit_step(bool *cont, int seqNum, int window_s, int window_length, int *windowStatusArr);
};
WTPHandler::WTPHandler(){
    error = 0;
};
WTPHandler::~WTPHandler(){
    error = 0;
};
int WTPHandler::getError(){
    return error;
};
struct PacketHeader WTPHandler::generatePacketHeader(unsigned int type, unsigned int seqNum, unsigned int length, char *datapiece) {
    return {type, seqNum, length, crc32(datapiece, length)};
}

void WTPHandler::copyPacketData(char *tmpBuffer, const struct PacketHeader *header, const char *datapiece) {
    size_t packet_header_len = sizeof(struct PacketHeader);
    memcpy(tmpBuffer, header, packet_header_len);
    memcpy(tmpBuffer + packet_header_len, datapiece, header->length);
}
size_t WTPHandler::createAndFillPacket(char *tmpBuffer, unsigned int type, unsigned int seqNum, unsigned int length, char *datapiece) {
    assert(datapiece != nullptr);
    size_t packet_header_len = sizeof(struct PacketHeader);
    assert(packet_header_len + length <= MAX_PACKET_LEN);

    struct PacketHeader header = generatePacketHeader(type, seqNum, length, datapiece);

    copyPacketData(tmpBuffer, &header, datapiece);

    return packet_header_len + length;
}
struct PacketHeader WTPHandler::parse_packet_header(char *tmpBuffer) {
    struct PacketHeader header;
    memcpy(&header, tmpBuffer, sizeof(struct PacketHeader));
    return header;
}

size_t WTPHandler::parse_chunk(char *tmpBuffer, char *datapiece) {
    // Parse packet header
    struct PacketHeader header = parse_packet_header(tmpBuffer); //create a packet header
    size_t packet_len = header.length;
    memcpy(datapiece, tmpBuffer + sizeof(struct PacketHeader), packet_len);
    return packet_len;
}

#include <stdio.h>

size_t WTPHandler::writeNthChunkToFile(char *dataChunk, int chunkNumber, size_t chunkSize, FILE *outputFile) {
    size_t maxChunkSize = MAX_PACKET_LEN - sizeof(struct PacketHeader);
    long offset = maxChunkSize * chunkNumber;

    // Check if fseek was successful
    if (fseek(outputFile, offset, SEEK_SET) == 0) {
        // Write the dataChunk directly without using fwrite for offset
        size_t bytesWritten = fwrite(dataChunk, chunkSize, 1, outputFile);
        if (bytesWritten == 1) {
            return chunkSize;
        } else {
            // Handle fwrite error
            // For example, you may print an error message or return an error code
            fprintf(stderr, "Error writing dataChunk to file\n");
            return 0; // Indicate failure
        }
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
        std::cerr << "Error seeking to offset " << offset << std::endl;
        return 0; // Indicate failure
    }
}




void WTPHandler::move_window(int *array, int num_elements, int step) {
    memmove(&array[0], &array[step], (num_elements - step) * sizeof(int));

    if (num_elements - step > 0) {
        memset(&array[num_elements - step], 0, step * sizeof(int));
    }

    for (int i = num_elements - step; i < num_elements; i++) {
        array[i] = 0;
    }
    std::cout << "Window moved\n";
}

void WTPHandler::writeLog(struct PacketHeader h,FILE *f){
    std::cout << h.type << " " << h.seqNum << " " << h.length << " " << h.checksum << std::endl;
    std::fprintf(f, "%d %d %d %d\n", h.type, h.seqNum, h.length, h.checksum);
}

void WTPHandler::commit_step(bool *cont, int seqNum, int window_s, int window_length, int *windowStatusArr) {
    // if cont is false, then change it to true, the cont should be change outside the function
    if (!*cont) {
        *cont = true;
    }
    std::cout << "Commit step\n";
    std::cout << "seqNum: " << seqNum << std::endl;
    std::cout << "window_s: " << window_s << std::endl;
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
    if (access(file_dir, F_OK) != 0) {
        std::cout << "Creating directory " << file_dir << std::endl;
        // if there are multiple levels of directories, create them
        std::filesystem::create_directories(file_dir);
    }
    FileWorker fileWorker;
    //initialize log file
    FILE *log_fileptr = fileWorker.open_file(log,"a+");

    // initialize WTPHandler
    WTPHandler wtpHandler;
    // Initialize UDP receiver
    int socketfd;
    struct sockaddr_in recv_address;
    struct sockaddr_in send_address;
    int addr_len = sizeof(struct sockaddr);
    int counter;
    char tmpBuffer[MAX_BUFFER_LEN];
    memset(tmpBuffer, 0, MAX_BUFFER_LEN);

    if ((socketfd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
        std::cerr << "Error creating socket\n";
        std::fclose(log_fileptr);
        return 1;
    }

    recv_address.sin_family = AF_INET;
    recv_address.sin_port = htons(port_num);
    recv_address.sin_addr.s_addr = INADDR_ANY;
    memset(&(recv_address.sin_zero), '\0', 8);

    bind(socketfd, reinterpret_cast<struct sockaddr *>(&recv_address), sizeof(struct sockaddr));

    int file_num = 0;
    int window_s = 0;
    int windowStatusArr[window_length]; // 0: not 
    while (true) {
        FILE *fileptr = nullptr;
        char datapiece[MAX_PACKET_LEN];
        bzero(datapiece, MAX_PACKET_LEN);
        char output_file[strlen(file_dir) + 15];
        sprintf(output_file, "%s/FILE-%d.out", file_dir, file_num++);

        int rand_num = -1;

        // Reset receive window
        memset(windowStatusArr, 0, window_length * sizeof(int));
        window_s = 0;

        bool completed = false;
        std::cout << "Waiting for packets...\n";
        while (!completed) {
            std::cout << "Waiting for packets...\n";
            assert(window_s >= 0);
            if ((counter = recvfrom(socketfd, tmpBuffer, MAX_BUFFER_LEN - 1, 0,
                                     (struct sockaddr *) &send_address, (socklen_t *) &addr_len)) == -1) {
                std::cerr << "Error receiving packet\n";
                exit(1);
            }
            printf("Received packet from %s:%d\n", inet_ntoa(send_address.sin_addr), ntohs(send_address.sin_port));


            char *send_ip = inet_ntoa(send_address.sin_addr);
            int send_port = ntohs(send_address.sin_port);

            struct PacketHeader header = wtpHandler.parse_packet_header(tmpBuffer);
            wtpHandler.writeLog(header,log_fileptr);

            memset(datapiece, 0, MAX_PACKET_LEN);
            size_t chunk_len = wtpHandler.parse_chunk(tmpBuffer, datapiece);

            if (crc32(datapiece, chunk_len) != header.checksum) {
                std::cerr << "Checksum mismatch\n" << std::endl;
                continue;
            }

            int seqNum = -1;
            bool cont = false;
            if (header.type == START) {
                if (rand_num != -1 && rand_num != header.seqNum) {
                    std::cerr << "Duplicate START\n";
                    wtpHandler.commit_step(&cont, seqNum, window_s, window_length, windowStatusArr);
                } else {
                    rand_num = header.seqNum;
                    seqNum = header.seqNum;
                    fileptr = fileWorker.openFileForReadWrite(output_file);
                }
            } else if (header.type == END) {
                if (rand_num != header.seqNum && rand_num != -1) {
                    std::cerr << "Duplicate END\n";
                    wtpHandler.commit_step(&cont, seqNum, window_s, window_length, windowStatusArr);
                } else {
                    seqNum = header.seqNum;
                    completed = true;
                    rand_num = -1;
                }
            } else if (header.type == DATA) {
                if (rand_num == -1) {
                    std::cerr << "No START\n" << std::endl;
                    wtpHandler.commit_step(&cont, seqNum, window_s, window_length, windowStatusArr);
                } else {
                    seqNum = window_s;
                    if (header.seqNum == seqNum) {
                        // header.seqNum == window_s
                        if (windowStatusArr[0] == 0) {
                            windowStatusArr[0] = 1;
                            assert(fileptr != nullptr);
                            wtpHandler.writeNthChunkToFile(datapiece, header.seqNum, chunk_len, fileptr);
                        }

                        int step = 0;
                        assert(window_length > 0);
                        for (int i = 0; i < window_length; i++) {
                            if (windowStatusArr[i] == 1) {
                                step++;
                                std::cout << "step: " << step << std::endl;
                                } else {
                                    break;
                                }
                        }
                        window_s = window_s + step;
                        seqNum = window_s;
                        wtpHandler.move_window(windowStatusArr, window_length, step);
                    }
                }
            } else {
                std::cerr << "Invalid packet type\n";
                wtpHandler.commit_step(&cont, seqNum, window_s, window_length, windowStatusArr);
            }
            if (cont) {
                continue;
            }

            // Init ACK sender
            struct sockaddr_in ACK_addr;
            struct hostent *otherHost;

            otherHost = gethostbyname(send_ip);
            assert(otherHost != nullptr);

            ACK_addr.sin_family = AF_INET;
            ACK_addr.sin_port = htons(send_port);
            assert(otherHost->h_addrtype == AF_INET);
            ACK_addr.sin_addr = *((struct in_addr *) otherHost->h_addr);
            memset(&(ACK_addr.sin_zero), '\0', 8);
            std::cout << "Sending ACK packet\n";

            char ACK_buffer[MAX_BUFFER_LEN];
            memset(ACK_buffer, 0, MAX_BUFFER_LEN);

            char empty_chunk[1];
            memset(empty_chunk, 0, 1);

            assert(seqNum >= 0);
            size_t ACK_packet_len = wtpHandler.createAndFillPacket(ACK_buffer, 3, seqNum, 0, empty_chunk);
            assert(ACK_packet_len <= MAX_PACKET_LEN);
            assert(ACK_packet_len > 0);

            if ((counter = sendto(socketfd, ACK_buffer, ACK_packet_len, 0,
                                   (struct sockaddr *) &ACK_addr, sizeof(struct sockaddr))) == -1) {
                std::cerr << "Error sending ACK packet\n";
                exit(1);
            }

            wtpHandler.writeLog(wtpHandler.parse_packet_header(ACK_buffer),log_fileptr);
        }
        std::cout << "File transfer complete\n";
        fclose(fileptr);
        if (completed) {
            std::cout << "File saved to " << output_file << std::endl;
            std::cout << "File size: " << filesize(output_file) << std::endl;
        }
    }
    std::cout << "Closing socket\n";
    close(socketfd);
    fclose(log_fileptr);

    return 0;
}
