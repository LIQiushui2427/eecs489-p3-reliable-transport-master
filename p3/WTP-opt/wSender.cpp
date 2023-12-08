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
#define DONE 1
int R;
int min(int x,int y){
    if(x < y) return x;
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
    // fseek(f, begin, SEEK_SET);
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
    char *receiver_ip = argv[1];
    int receiver_port = atoi(argv[2]);
    int window_size = atoi(argv[3]);
    FILE *f_input = fopen(argv[4], "r");
    FILE *f_log = open_log(argv[5]);

    fseek(f_input, 0, SEEK_END);
    long file_size = ftell(f_input);
    int total_amount_of_contents = ceil((double) file_size / (WTP_DATA_LEN));
    rewind(f_input);
    // window, -1 = pending, 0 = sent, 1 = acked
    // time init
    int sliding_window[window_size];
    struct timeval packet_sent_time[window_size];
    for(int i=0;i<window_size;i++) {sliding_window[i]=-1;packet_sent_time[i] = (struct timeval) {0};}
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
    // sent content!
    unsigned int window_start = 0;
    int resend = 1;
    // init buffer
    char content[BUF_SIZE];memset(content,'\0',BUF_SIZE);char packet[BUF_SIZE];memset(packet,'\0',BUF_SIZE);char ack_reply[BUF_SIZE];memset(ack_reply,'\0',BUF_SIZE);

    while (window_start != total_amount_of_contents) {
        /*
        for (int i = 0; i < min(window_size, total_amount_of_contents - window_start); i++) {
            if (sliding_window[i] == NOT_SENT || (flag && sliding_window[i] == NOT_ACKED)) {
                unsigned int content_len = get_content_at(i+window_start,content,f_input,file_size);
                unsigned int packet_len = get_packet(packet,content,2,i+window_start,content_len);
                sendto(socket_fd, packet, packet_len, 0,(struct sockaddr *) &receiver_addr, sizeof(struct sockaddr));
                sliding_window[i] = NOT_ACKED;
                prt_log(get_packet_header(packet),f_log);
            }
        }
        // get time for first recv!
        if (flag) gettimeofday(&t, NULL);
        // may recv many useless packets!
        unsigned int byte_recved = recvfrom(socket_fd, ack_reply, BUF_SIZE-1, 0,(struct sockaddr *) &ack_reply_addr, (socklen_t *) &addr_len);
        // error when sent
        // or in case where NOAck
        if (byte_recved == -1) {
            flag = 1;continue;
        }
        flag = 0;
        struct PacketHeader h = get_packet_header(ack_reply);prt_log(h,f_log);
        if (h.type == 3 && h.seqNum > window_start) {
            #ifndef OPTION
            int k = h.seqNum-window_start;
            for(int s=k;s<=window_size-1;s++) sliding_window[s-k]=sliding_window[s];
            for(int s=window_size-k;s<=window_size-1;s++) sliding_window[s]=NOT_SENT;
            #else
            flag = 1;
            for(int s=0;s<=window_size-1;s++) sliding_window[s] = NOT_SENT;
            #endif
            window_start = h.seqNum;
            gettimeofday(&t, NULL);
            continue;
        }
        // recv other useless packet
        struct timeval temp;gettimeofday(&temp, NULL);
        if((temp.tv_sec - t.tv_sec) + (temp.tv_usec - t.tv_usec) / 1000000.0 > 0.5) flag = 1;
        */
        // sent packet
        for (int i = 0; i < min(window_size, total_amount_of_contents - window_start); i++) {
            if(sliding_window[i]==NOT_SENT){
                unsigned int content_len = get_content_at(i+window_start,content,f_input,file_size);
                unsigned int packet_len = get_packet(packet,content,2,i+window_start,content_len);
                sendto(socket_fd, packet, packet_len, 0,(struct sockaddr *) &receiver_addr, sizeof(struct sockaddr));
                prt_log(get_packet_header(packet),f_log);
                sliding_window[i]=NOT_ACKED;
                gettimeofday(&packet_sent_time[i],NULL);
            }else if(sliding_window[i]==NOT_ACKED){
                gettimeofday(&t, NULL);
                double d = (t.tv_sec - packet_sent_time[i].tv_sec)*1000 + (t.tv_usec - packet_sent_time[i].tv_usec) / 1000.0;
                if(d>500){
                    unsigned int content_len = get_content_at(i+window_start,content,f_input,file_size);
                    unsigned int packet_len = get_packet(packet,content,2,i+window_start,content_len);
                    sendto(socket_fd, packet, packet_len, 0,(struct sockaddr *) &receiver_addr, sizeof(struct sockaddr));
                    prt_log(get_packet_header(packet),f_log);
                    gettimeofday(&packet_sent_time[i],NULL);
                }
            }
        }
        // recv packet
        for (int i = 0; i < min(window_size, total_amount_of_contents - window_start); i++) {
            unsigned int byte_recved = recvfrom(socket_fd, ack_reply, BUF_SIZE-1, 0,(struct sockaddr *) &ack_reply_addr, (socklen_t *) &addr_len);
            if(byte_recved != -1){
                struct PacketHeader h = get_packet_header(ack_reply);
                prt_log(h,f_log);
                if (h.type == 3 && h.seqNum >= window_start && (h.seqNum < window_start + window_size))
                    sliding_window[h.seqNum - window_start] = DONE;
            }
        }
        int k=0;
        while(k < min(window_size, total_amount_of_contents - window_start) && (sliding_window[k] == DONE)) k++;
        for(int s=k;s<=window_size-1;s++){
            sliding_window[s-k]=sliding_window[s];
            packet_sent_time[s-k]=packet_sent_time[s];
        }
        for(int s=window_size-k;s<=window_size-1;s++) {
            sliding_window[s]=NOT_SENT;
            packet_sent_time[s]=(struct timeval) {0};
        }
        window_start+=k;
    }
    // udp disconnect!
    udp_disconnect(socket_fd,&receiver_addr,&ack_reply_addr,&addr_len,f_log);
}
