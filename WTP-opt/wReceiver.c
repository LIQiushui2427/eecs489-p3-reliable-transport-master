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
