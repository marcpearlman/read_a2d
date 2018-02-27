#ifndef __READ_A2D_H
#define __READ_A2D_H

#include <stdio.h>        // printf(), sprintf(), perror()
                          // fopen(), fclose()
                          // FILE, size_t, ssize_t
#include <stdlib.h>       // exit()
#include <unistd.h>       // read(), close()
#include <string.h>       // memset()
#include <signal.h>       // sigaction(), struct sigaction
#include <pthread.h>      // pthread_create(), pthread_join()
#include <stdint.h>       // uint64_t, int16_t, etc.
#include <endian.h>       // le16toh()
#include <sys/cdefs.h>    // likely(), unlikely()    

#include <time.h>         // struct timespec, struct itimerspec
                          // clock_gettime()

#include <sys/timerfd.h>  // timerfd_create(), timerfd_settime()


#include <linux/if_ether.h>  // ETH_ALEN
#include <netdb.h>           // struct addrinfo, getaddrinfo()
#include <sys/socket.h>      // socket(), sendto()


#ifndef likely
    #define likely(x)     __builtin_expect(!!(x), 1)
#endif

#ifndef unlikely
    #define unlikely(x)   __builtin_expect(!!(x), 0)
#endif


#define A2D_MIN_CHANNEL_ID	4      // a2d channel ids range 4-7
#define A2D_MAX_CHANNEL_ID      7      // a2d channel ids range 4-7

#define A2D_NUM_CHANNELS        (A2D_MAX_CHANNEL_ID - A2D_MIN_CHANNEL_ID + 1)

// data structures
struct a2d_channel_t
{
        uint8_t channel_id;        // a2d channel_id

        char name[80];
        char full_path[200];
        FILE *fp;
 
        uint16_t data;
};


// Header and data block structures for message to remote server
struct a2d_msg_header_t
{
        uint8_t mac_addr[ETH_ALEN];
        uint8_t version;
        uint8_t num_data_blocks;
        uint8_t a2d_channel_id[A2D_NUM_CHANNELS];
};

struct a2d_msg_data_t
{
        uint32_t sample_id;
        uint32_t start_time_sec;
        uint32_t start_time_nsec;
        uint32_t stop_time_sec;
        uint32_t stop_time_nsec;
        uint16_t a2d_channel_data[A2D_NUM_CHANNELS];
        uint16_t msg_reserved_1[3 - ((A2D_NUM_CHANNELS-1) & 3)];  // reserved to maintain  nignment
};

#endif // #ifndef __READ_A2D_H
