#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <math.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/stat.h>
#include "PacketHeader.h"
#include "crc32.h"

#define BUF_SIZE 2048
#define WTP_WITH_HEADER_LEN 1472
#define WTP_DATA_LEN (WTP_WITH_HEADER_LEN - sizeof(struct PacketHeader))
#define NOT_SENT -1
#define NOT_ACKED 0
//#define OPTION 0

int R;
int min(int x,int y){
    if(x < y) return x;
    return y;
}
int max(int x,int y){
    if(x > y) return x;
    return y;
}
struct PacketHeader get_packet_header(char * buffer){
    struct PacketHeader h;memcpy(&h, buffer, sizeof(struct PacketHeader));
    return h;
}
// length is the content len, return total len
unsigned int get_packet(char *packet,char *content,unsigned int type, unsigned int seqNum,unsigned int len){
    struct PacketHeader ph;
    ph.type=type;ph.seqNum=seqNum;ph.length=len;ph.checksum= crc32(content,len);
    memcpy(packet, &ph, sizeof(struct PacketHeader));
    memcpy(packet + sizeof(struct PacketHeader), content, len);
    return sizeof(struct PacketHeader) + len;
}
// len = len of file
unsigned int get_content_at(int i,char *content,FILE *f,long file_size){
    long begin = i * WTP_DATA_LEN;long end = begin + WTP_DATA_LEN - 1;
    if(end >= file_size) end = file_size-1;
    long read_len = end - begin + 1;
    // offset
    // fseek(f, begin - file_size, SEEK_CUR);
    fseek(f, begin - ftell(f), SEEK_CUR);
    fread(content, read_len, sizeof(char), f);
    return read_len;
}

void prt_log(struct PacketHeader h,FILE *f){
    fprintf(f, "%u %u %u %u\n", h.type,h.seqNum,h.length,h.checksum);fflush(f);
}
void udp_connect(int sockfd,struct sockaddr_in* receiver_addr,struct sockaddr_in* ack_reply_addr,int* addr_len,FILE *f){
    // init variables
    char start_packet[BUF_SIZE];char content[BUF_SIZE];char ack_packet[BUF_SIZE];
    memset(start_packet,'\0',BUF_SIZE);memset(content,'\0',BUF_SIZE);memset(ack_packet,'\0',BUF_SIZE);
    // construct START packet
    unsigned int len = get_packet(start_packet,content,0,R,0);
    unsigned long bytes_recved;
    // try connection until we success
    while(1){
        // send START and prt to log
        sendto(sockfd, start_packet, len, 0,(struct sockaddr *)receiver_addr, sizeof(struct sockaddr));
        prt_log(get_packet_header(start_packet),f);
        // recv ACK
        bytes_recved = recvfrom(sockfd, ack_packet, BUF_SIZE-1, 0,(struct sockaddr *) ack_reply_addr, (socklen_t *)addr_len);
        if(bytes_recved == -1) continue;
        // get ACK and prt to log
        struct PacketHeader h = get_packet_header(ack_packet);prt_log(h,f);
        // connected!
        if (h.type == 3 && h.seqNum == R) return;
    }
}
void udp_disconnect(int sockfd,struct sockaddr_in* receiver_addr,struct sockaddr_in* ack_reply_addr,int* addr_len,FILE *f){
    char end_packet[BUF_SIZE];char content[BUF_SIZE];char ack_packet[BUF_SIZE];
    memset(end_packet,'\0',BUF_SIZE);memset(content,'\0',BUF_SIZE);memset(ack_packet,'\0',BUF_SIZE);
    unsigned int len = get_packet(end_packet,content,1,R,0);
    unsigned long bytes_recved;
    while(1){
        sendto(sockfd, end_packet, len, 0,(struct sockaddr *)receiver_addr, sizeof(struct sockaddr));
        prt_log(get_packet_header(end_packet),f);
        bytes_recved = recvfrom(sockfd, ack_packet, BUF_SIZE-1, 0,(struct sockaddr *) ack_reply_addr, (socklen_t *)addr_len);
        if(bytes_recved == -1) continue;
        struct PacketHeader h = get_packet_header(ack_packet);prt_log(h,f);
        // disconnected!
        if (h.type == 3 && h.seqNum == R) return;
    }
}

int get_socket(struct timeval* timeout){
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, timeout, sizeof(*timeout));
    return sockfd;
}

FILE* open_log(char *path){
    int len = strlen(path);
    for(int i=0;i<len;i++){
        if(path[i]=='/'){
            path[i]=0;
            if (access(path, F_OK) != 0) mkdir(path, 0777);
            path[i]='/';
        }
    }
    FILE *f_log=fopen(path, "a+");
    return f_log;
}

int main(int argc, char *argv[]) {
    printf("start!\n");
    char *receiver_ip = argv[1];
    int receiver_port = atoi(argv[2]);
    int window_size = atoi(argv[3]);
    // must exist
    FILE *f_input = fopen(argv[4], "r");
    // may not exist
    // FILE *f_log = fopen(argv[5], "a+");
    FILE *f_log = open_log(argv[5]);

    fseek(f_input, 0, SEEK_END);
    long file_size = ftell(f_input);
    int total_amount_of_contents = ceil((double) file_size / (WTP_DATA_LEN));
    rewind(f_input);
    // window, -1 = pending, 0 = sent, 1 = acked
    int sliding_window[window_size];
    for(int i=0;i<window_size;i++) sliding_window[i]=NOT_SENT;
    // rand
    R = rand();
    // socket issues
    struct sockaddr_in receiver_addr;
    struct sockaddr_in ack_reply_addr;
    int addr_len = sizeof(struct sockaddr);
    memset(&receiver_addr, 0, sizeof(receiver_addr));
    receiver_addr.sin_family = AF_INET;
    receiver_addr.sin_port = htons(receiver_port);
    inet_pton(AF_INET, receiver_ip, &(receiver_addr.sin_addr));
    // timeout and t
    struct timeval timeout;
    struct timeval t;
    timeout.tv_sec = 0;timeout.tv_usec = 500000;
    int socket_fd = get_socket(&timeout);
    // udp connect!
    udp_connect(socket_fd,&receiver_addr,&ack_reply_addr,&addr_len,f_log);
    printf("connected!\n");
    // sent content!
    unsigned int window_start = 0;
    int flag = 1;
    // init buffer
    char content[BUF_SIZE];memset(content,'\0',BUF_SIZE);char packet[BUF_SIZE];memset(packet,'\0',BUF_SIZE);char ack_reply[BUF_SIZE];memset(ack_reply,'\0',BUF_SIZE);

    // 0 1 2 3 ... total_amount_of_contents-1
    printf("cur_win=%d, tot_win=%d\n",window_start,total_amount_of_contents);
    int acked;
    while(window_start < total_amount_of_contents){
        printf("cur_win=%d, tot_win=%d\n",window_start,total_amount_of_contents);
        if(flag) {
            printf("sent!\n");
            for (int i = 0; i < min(window_size, total_amount_of_contents - window_start); i++) {
                unsigned int content_len = get_content_at(i + window_start, content, f_input, file_size);
                unsigned int packet_len = get_packet(packet, content, 2, i + window_start, content_len);
                sendto(socket_fd, packet, packet_len, 0, (struct sockaddr *) &receiver_addr, sizeof(struct sockaddr));
                prt_log(get_packet_header(packet), f_log);
            }
            gettimeofday(&t, NULL);
            acked = 0;
        }
        // recv for 500 ms
        unsigned int byte_recved = recvfrom(socket_fd, ack_reply, BUF_SIZE-1, 0,(struct sockaddr *) &ack_reply_addr, (socklen_t *) &addr_len);
        if(byte_recved == -1){
            // must timeout
            flag = 1;
            acked= 0;
            continue;
        }else{
            // if correct packet then
            // reset timer, flag = 1, do offset
            // else
            // drop useless one, test timeout, if timeout then flag=1, else continue
            flag = 0;
            struct PacketHeader h = get_packet_header(ack_reply);prt_log(h,f_log);
            if(h.type==3 && (h.seqNum >= window_start)) {
                acked = max(h.seqNum - window_start, acked);
                if ((acked >= window_size || (window_start + acked >= total_amount_of_contents))) {
                    flag = 1;
                    window_start += acked;
                    acked = 0;
                    continue;
                }
            }
            struct timeval cur_t;gettimeofday(&cur_t,NULL);
            if((cur_t.tv_sec - t.tv_sec) + (cur_t.tv_usec - t.tv_usec) / 1000000.0 >= 0.5){
                flag = 1;
                window_start+=acked;
                acked=0;
                continue;
            }
        }
    }
    // udp disconnect!
    udp_disconnect(socket_fd,&receiver_addr,&ack_reply_addr,&addr_len,f_log);
    close(socket_fd);
}
