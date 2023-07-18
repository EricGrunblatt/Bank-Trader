#include <time.h>

#include "protocol.h"
#include "csapp.h"
#include "debug.h"


/*
 * Send a packet, which consists of a fixed-size header followed by an
 * optional associated data payload.
 *
 * @param fd  The file descriptor on which packet is to be sent.
 * @param pkt  The fixed-size packet header, with multi-byte fields
 *   in network byte order
 * @param data  The data payload, or NULL, if there is none.
 * @return  0 in case of successful transmission, -1 otherwise.
 *   In the latter case, errno is set to indicate the error.
 *
 * All multi-byte fields in the packet are assumed to be in network byte order.
 */
int proto_send_packet(int fd, BRS_PACKET_HEADER *hdr, void *payload) {
    // Check if header and file descriptor are valid
    if(fd < 0 || hdr == NULL) return EXIT_FAILURE;

    // Get the current time in seconds and nanoseconds
    struct timespec currTime;
    timespec_get(&currTime, TIME_UTC);

    // Set the times in seconds and nanoseconds to hdr (use htonl to convert from host to network)
    hdr->timestamp_sec = htonl(currTime.tv_sec);
    hdr->timestamp_nsec = htonl(currTime.tv_nsec);

    // Write to the file descriptor
    if(Write(fd, hdr, sizeof(BRS_PACKET_HEADER)) < sizeof(BRS_PACKET_HEADER)) return EXIT_FAILURE;

    // Running over network connection with uint_16 (use ntohs for size attribute)
    uint16_t pktSize = ntohs(hdr->size);

    // If size > 0, write the additional payload
    if(pktSize) {
        if(Write(fd, payload, (size_t) pktSize) < pktSize) return EXIT_FAILURE;
    }
    

    return EXIT_SUCCESS;
}

/*
 * Receive a packet, blocking until one is available.
 *
 * @param fd  The file descriptor from which the packet is to be received.
 * @param pkt  Pointer to caller-supplied storage for the fixed-size
 *   portion of the packet.
 * @param datap  Pointer to a variable into which to store a pointer to any
 *   payload received.
 * @return  0 in case of successful reception, -1 otherwise.  In the
 *   latter case, errno is set to indicate the error.
 *
 * The returned packet has all multi-byte fields in network byte order.
 * If the returned payload pointer is non-NULL, then the caller has the
 * responsibility of freeing that storage.
 */
int proto_recv_packet(int fd, BRS_PACKET_HEADER *hdr, void **payloadp) {
    // Read the data from the file descriptor
    if(Read(fd, hdr, sizeof(BRS_PACKET_HEADER)) < sizeof(BRS_PACKET_HEADER)) return EXIT_FAILURE;
    
    // Get the current time in seconds and nanoseconds
    struct timespec currTime;
    timespec_get(&currTime, TIME_UTC);

    // Set the times in seconds and nanoseconds to hdr (use ntohl to convert from network to host)
    hdr->timestamp_sec = ntohl(currTime.tv_sec);
    hdr->timestamp_nsec = ntohl(currTime.tv_nsec);

    // Set the data into the hdr pointer
    uint16_t pktSize = ntohs(hdr->size);

    // If hdr size > 0, set the payload pointer
    if(pktSize) { 
        *payloadp = Malloc(pktSize);
        memset(*payloadp, 0, sizeof(pktSize));
        if(Read(fd, *payloadp, pktSize) < pktSize) return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
