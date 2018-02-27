#include "read_a2d.h"


// Global variables
int fd_timer_read_a2d;	     // file descriptor for a2d read timer
int fd_timer_msg_send;       // file descriptor for msg send timer

pthread_t pthread_read_a2d;  // pthread for reading a2d, triggered by a2d timer
pthread_t pthread_msg_send;  // pthread for sending a2d data to remote server

pthread_mutex_t msg_mutex;   // mutex protecting access to buffered a2d data
                             // to be reported to remote host

char msg_buf[1000];
struct a2d_msg_header_t *a2d_msg_header=NULL;
struct a2d_msg_data_t   *a2d_msg_data=NULL;
uint8_t num_data_blocks = 0;


// Functions
static int read_timerfd(int fd, uint64_t *timer_num_expirations)
{
        int            retval_read_timerfd = 0;

        ssize_t        bytes_read, bytes_read_total = 0;


        // Unlike the general case of read(), a timerfd read() should
        //   NEVER result in a partial read.
        //   (ie. unless there is an error, the number of bytes read should
        //   always be equal to the number of bytes requested.) 
        //
        // This function will return a negative values in case of:
        //   - general read error
        //   - read overrun  (reading too many bytes - should never happen)
        //   - partial reads (reading too few bytes  - should never happen)

        bytes_read = read(fd, (void *)timer_num_expirations, sizeof(uint64_t));
        if(unlikely(bytes_read < 0))
        {
                perror("read_timerfd(...): ");
                retval_read_timerfd = -1;
                goto end;
        }

        bytes_read_total += bytes_read;
        if(unlikely(bytes_read_total > sizeof(uint64_t)))
        {
                perror("read_timerfd(...): too many bytes read");
                retval_read_timerfd = -2;
        }
        else if(unlikely(bytes_read_total < sizeof(uint64_t)))
        {
                perror("read_timerfd(...): more bytes remaining");
                retval_read_timerfd = -3;
        }

end:
        return(retval_read_timerfd);
}


static void *msg_send(void *thread_data)
{
        int   retval_int;
        ssize_t retval_ssize_t;
        void *retval_msg_send = (void *)0;

        uint64_t timer_num_expirations;         // number of times that a2d timer has expired
                                                // since last iteration of main loop
        uint64_t timer_num_expirations_total=0; // total number of times that a2d timer has expired

        FILE *fp_mac_address;
        char wifi_mac_address_file[100]="/sys/class/net/wlan0/address";
        
	int fd_sock;
        struct addrinfo hints, *servinfo, *addr_info_ptr=NULL;

        char server_ip_addr_or_name_str[240]="192.168.1.100";
        char server_port_str[10]="5231";

        size_t msg_buf_len_bytes;

        int i;


        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_DGRAM;

        retval_int = getaddrinfo(server_ip_addr_or_name_str,
                                 server_port_str,
                                 &hints, &servinfo);
        if(retval_int !=0)
        {
                perror("msg_send(...): getaddrinfo(...)");
                retval_msg_send = (void *)10;
                goto end;
        }
        
        for(addr_info_ptr = servinfo; addr_info_ptr != NULL; addr_info_ptr = addr_info_ptr->ai_next)
        {
                fd_sock = socket(addr_info_ptr->ai_family,
                                 addr_info_ptr->ai_socktype,
                                 addr_info_ptr->ai_protocol);
                if(unlikely(fd_sock == -1))
                        continue;
                break;
        }

        if(unlikely(addr_info_ptr == NULL))
        {
                perror("msg_send(...): no socket could be created");
                retval_msg_send = (void *)20;
                goto end;
        }

        a2d_msg_header = (struct a2d_msg_header_t *)msg_buf;
        a2d_msg_data   = (struct a2d_msg_data_t *)(a2d_msg_header + 1);

        fp_mac_address = fopen(wifi_mac_address_file, "r");
        if(unlikely(fp_mac_address == NULL))
        {
                perror("msg_send(...): fopen(wifi_mac_address_file,\"r\")");
                retval_msg_send = (void *)30;
                goto end;
        }

        retval_int = fscanf(fp_mac_address,
                            "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                             &(a2d_msg_header->mac_addr[0]),
                             &(a2d_msg_header->mac_addr[1]),
                             &(a2d_msg_header->mac_addr[2]),
                             &(a2d_msg_header->mac_addr[3]),
                             &(a2d_msg_header->mac_addr[4]),
                             &(a2d_msg_header->mac_addr[5]));
        if(unlikely(retval_int < 0))
        {
                perror("msg_send(...): fscanf(..., &(a2d_msg_header->mac_addr[]))");
                retval_msg_send = (void *)40;
                goto end;
        }
        else if(unlikely(retval_int < 6))
        {
                perror("msg_send(...): fscanf(..., &(a2d_msg_header->mac_addr[])): too few input items returned");
                retval_msg_send = (void *)41;
                goto end;
        }
        else if(unlikely(retval_int > 6))
        {
                perror("msg_send(...): fscanf(..., &(a2d_msg_header->mac_addr[])): too many input items returned");
                retval_msg_send = (void*)42;
                goto end;
        }

        retval_int = fclose(fp_mac_address);
        if(unlikely(retval_int !=0))
        {
                perror("msg_send(...): fclose(fp_mac_address)");
                retval_msg_send = (void *)50;
                goto end;
        }

        a2d_msg_header->version = 0x01;
        for(i=0; i< A2D_NUM_CHANNELS; i++)
                a2d_msg_header->a2d_channel_id[i] = A2D_MIN_CHANNEL_ID + i;

        pthread_mutex_lock(&msg_mutex);
        num_data_blocks = 0;
        pthread_mutex_unlock(&msg_mutex);

        while(1)
        {
                // This function blocks until timer has expired.
                // The fd_timer accesses the number of times that this timer has
                //   expired since the fd_timer was last read.
                // Ideally, this value should be 1.   However, if the loop
                //   takes too long to execute a/o the timer interval is
                //   too short, the timer may expire (perhaps multiple times)
                //   prior to the next fd_timer read.
                retval_int = read_timerfd(fd_timer_msg_send, &timer_num_expirations);
                if(unlikely(retval_int < 0))
                {
                        perror("msg_send(...): read_timerfd(...)");
                        retval_msg_send = (void *)60;
                        goto end;
                }

                timer_num_expirations_total += timer_num_expirations;

                pthread_mutex_lock(&msg_mutex);
                msg_buf_len_bytes = sizeof(struct a2d_msg_header_t) + \
                                    num_data_blocks * sizeof(struct a2d_msg_data_t);

                if(unlikely(msg_buf_len_bytes > 1000))
                {
                        perror("msg_send(...): message buffer overflow");
                        retval_msg_send = (void *)70;
                        pthread_mutex_unlock(&msg_mutex);
                        goto end;
                }

                a2d_msg_header->num_data_blocks = num_data_blocks;

                retval_ssize_t = sendto(fd_sock,
                                        msg_buf, msg_buf_len_bytes, 0, 
                                        addr_info_ptr->ai_addr,
                                        addr_info_ptr->ai_addrlen);

                if(unlikely(retval_ssize_t == -1))
                {
                        perror("msg_send(...): sendto(...)");
                        retval_msg_send = (void *)80;
                        pthread_mutex_unlock(&msg_mutex);
                        goto end;
                }

                num_data_blocks = 0;
                pthread_mutex_unlock(&msg_mutex);
        }

end:
        return(retval_msg_send);
}


static void *read_a2d(void *thread_data)
{
        int  retval_int;
        void *retval_read_a2d = (void *)0;

        char a2d_sysfs_dir[100] = "/sys/bus/iio/devices/iio:device0/";

        struct a2d_channel_t a2d_channel[A2D_NUM_CHANNELS];


        uint64_t timer_num_expirations;         // number of times that a2d timer has expired
                                                // since last iteration of main loop
        uint64_t timer_num_expirations_total=0; // total number of times that a2d timer has expired

        uint32_t sample_id = 0;

        uint16_t raw_data;	         // raw data as read from a2d output

        struct timespec start_ts, stop_ts;

        uint8_t i, idx;

        struct a2d_msg_data_t *a2d_current_data_block;

        // initialize a2d channel parameters
        for(i=0; i<A2D_NUM_CHANNELS; i++)
        {
                // a2d channel id
                a2d_channel[i].channel_id = i + A2D_MIN_CHANNEL_ID;


                // name of raw a2d channel
                retval_int = sprintf(a2d_channel[i].name,
                                     "in_voltage%hhu_raw",
                                      a2d_channel[i].channel_id);
                if(unlikely(retval_int < 0))
                {
                        perror("read_a2d(...): sprintf(a2d_channel[i].name,...)");
                        retval_read_a2d = (void *)10;
                        goto end;
                }

                // full path of raw a2d channel
                retval_int = sprintf(a2d_channel[i].full_path,
                                     "%s%s",
                                     a2d_sysfs_dir,
                                     a2d_channel[i].name);
                if(unlikely(retval_int < 0)) 
                {
                        perror("read_a2d(...): sprintf(a2d_channel[i].full_path)");
                        retval_read_a2d = (void *)20;
                        goto end;
                }
        }

        while(1)
        {
                // This function blocks until timer has expired.
                // The fd_timer accesses the number of times that this timer has
                //   expired since the fd_timer was last read.
                // Ideally, this value should be 1.   However, if the loop
                //   takes too long to execute a/o the timer interval is
                //   too short, the timer may expire (perhaps multiple times)
                //   prior to the next fd_timer read.
                retval_int = read_timerfd(fd_timer_read_a2d, &timer_num_expirations);
                if(unlikely(retval_int < 0))
                {
                        perror("read_a2d(...): read_timerfd(...)");
                        retval_read_a2d = (void *)30;
                        goto end;
                }

                timer_num_expirations_total += timer_num_expirations;


                // Acquire time stamp before reading a2d channels
                retval_int = clock_gettime(CLOCK_REALTIME, &start_ts);
                if(unlikely(retval_int == -1))
                {
                        perror("read_a2d(...): clock_gettime(..., &start_ts)");
                        retval_read_a2d = (void *)40;
                        goto end;
                }


                // Read all a2d channel outputs
                for(i=0; i<A2D_NUM_CHANNELS; i++)
                {
                        a2d_channel[i].fp = fopen(a2d_channel[i].full_path, "r");
                        if(unlikely(a2d_channel[i].fp == NULL))
                        {
                                perror("read_a2d(...): fopen(a2d_channel[i].full_path, \"r\")");
                                retval_read_a2d = (void *)50;
                                goto end;
                        }


                        retval_int = fscanf(a2d_channel[i].fp, "%hu", &raw_data);
                        if(unlikely(retval_int < 0))
                        {
                                perror("read_a2d(...): fscanf(..., &raw_data)");
                                retval_read_a2d = (void *)60;
                                goto end;
                        }
                        else if(unlikely(retval_int < 1))
                        {
                                perror("read_a2d(...): fscanf(..., &raw_data): too few input items returned");
                                retval_read_a2d = (void *)61;
                                goto end;
                        }
                        else if(unlikely(retval_int > 1))
                        {
                                perror("read_a2d(...): fscanf(..., &raw_data): too many input items returned");
                                retval_read_a2d = (void*)62;
                                goto end;
                        }


                        retval_int = fclose(a2d_channel[i].fp);
                        if(unlikely(retval_int != 0))
                        {
                                perror("read_a2d(...): fclose(a2d_channel[i].fp)");
                                retval_read_a2d = (void *)70;
                                goto end;
                        }

                        // a2d output is a 12 bit value 
                        //   (reported as a 16 bit litte-endian value) 
                        // Convert to endianness of host.
                        a2d_channel[i].data = le16toh(raw_data);
                }

                // Acquire time stamp after reading a2d channels
                retval_int = clock_gettime(CLOCK_REALTIME, &stop_ts);
                if(unlikely(retval_int == -1))
                {
                        perror("read_a2d(...): clock_gettime(..., &stop_ts)");
                        retval_read_a2d = (void *)100;
                        goto end;
                }


                // Check if msg header and data block pointers 
                // have been assigned
                if(unlikely(a2d_msg_header == NULL))
                        continue;

		if(unlikely(a2d_msg_data == NULL))
                        continue;


                // Assign data to msg header and current data block
                pthread_mutex_lock(&msg_mutex);

                num_data_blocks++;
                idx = num_data_blocks-1;

                a2d_current_data_block = a2d_msg_data + idx;
                a2d_current_data_block->sample_id = sample_id++;
                a2d_current_data_block->start_time_sec =  (uint32_t)start_ts.tv_sec;
                a2d_current_data_block->start_time_nsec = (uint32_t)start_ts.tv_nsec;
                a2d_current_data_block->stop_time_sec =   (uint32_t)stop_ts.tv_sec;
                a2d_current_data_block->stop_time_nsec =  (uint32_t)stop_ts.tv_nsec;
                for(i=0; i<A2D_NUM_CHANNELS; i++)
                {
                        a2d_current_data_block->a2d_channel_data[i] = \
                                a2d_channel[i].data;
                }
                pthread_mutex_unlock(&msg_mutex);

#if 0                
                printf("%llu: ", timer_num_expirations_total);
                printf("(%d.%09d)", (int)start_ts.tv_sec, (int)start_ts.tv_nsec);
                printf("(%d.%09d)", (int)stop_ts.tv_sec,  (int)stop_ts.tv_nsec);
                for(i=0; i<A2D_NUM_CHANNELS; i++)
                        printf(" %hu", a2d_channel[i].data);
                printf("\n");
#endif
        }

end:
        return(retval_read_a2d);
}

static void sig_handler(int sig_num)
{
        int retval_int;

        if(sig_num == SIGINT)
        {
                retval_int = pthread_cancel(pthread_msg_send);
                if(unlikely(retval_int !=0))
                {
                        perror("sig_handler(...): pthread_cancel(pthread_msg_send)");
                }

                retval_int = pthread_cancel(pthread_read_a2d);
                if(unlikely(retval_int !=0))
                {
                        perror("sig_handler(...): pthread_cancel(pthread_read_a2d)");
                }
        }
}


int main(int argc, char **argv)
{
        int             retval_int;
        void           *retval_pthread;
        int             retval_main = 0;

        struct sigaction sig_action;

        struct itimerspec timer_info_read_a2d, timer_info_msg_send;


        // Register signal handler for SIGINT (Ctrl+C)
        memset(&sig_action, 0, sizeof(struct sigaction));
        sig_action.sa_handler = sig_handler;
        retval_int = sigaction(SIGINT, &sig_action, NULL);
        if(unlikely(retval_int == -1))
        {
                perror("main(...): sig_action(...)");
                retval_main = 20;
                goto end;
        }

        // Assign initial start time for read_a2d timer
        // (start time is relative to current time)
        timer_info_read_a2d.it_value.tv_sec = 1;
        timer_info_read_a2d.it_value.tv_nsec = 0;

        // Assign read_a2d timer interval (period)
        timer_info_read_a2d.it_interval.tv_sec = 0;
        timer_info_read_a2d.it_interval.tv_nsec = 500000000;

        // Create and set a2d timer 
        fd_timer_read_a2d = timerfd_create(CLOCK_REALTIME, 0);        
        if(unlikely(fd_timer_read_a2d == -1))
        {
                perror("main(...): fd_timer_read_a2d = timerfd_create(...)");
                retval_main = 30;
                goto end;
        }

        retval_int = timerfd_settime(fd_timer_read_a2d, 0, &timer_info_read_a2d, NULL);
        if(unlikely(retval_int == -1))
        {
                perror("main(): timerfd_settime(fd_timer_read_a2d, ...)");
                retval_main = 40;
                goto end;
        }


        // Assign initial start time for msg_send timer
        // (start time is relative to current time)
        timer_info_msg_send.it_value.tv_sec = 1;
        timer_info_msg_send.it_value.tv_nsec = 25000000;

        // Assign msg_send timer interval (period)
        timer_info_msg_send.it_interval.tv_sec = 2;
        timer_info_msg_send.it_interval.tv_nsec = 0;

        // Create and set msg_send timer 
        fd_timer_msg_send = timerfd_create(CLOCK_REALTIME, 0);        
        if(unlikely(fd_timer_msg_send == -1))
        {
                perror("main(...): fd_timer_msg_send = timerfd_create(...)");
                retval_main = 50;
                goto end;
        }

        retval_int = timerfd_settime(fd_timer_msg_send, 0, &timer_info_msg_send, NULL);
        if(unlikely(retval_int == -1))
        {
                perror("main(): timerfd_settime(fd_timer_msg_send, ...)");
                retval_main = 60;
                goto end;
        }



        pthread_mutex_init(&msg_mutex, NULL);


        // Create pthread for reading a2d values when a2d timer expires
        retval_int = pthread_create(&pthread_read_a2d, NULL, read_a2d, (void *)NULL);
        if(unlikely(retval_int != 0))
        {
                perror("main(...): pthread_create(pthread_read_a2d, ...)");
                retval_main = 70;
                goto end;
        }

        // Create pthread for sending msgs containing a2d samples to remote server
        retval_int = pthread_create(&pthread_msg_send, NULL, msg_send, (void *)NULL);
        if(unlikely(retval_int != 0))
        {
                perror("main(...): pthread_create(pthread_msg_send, ...)");
                retval_main = 80;
                goto end;
        }


        // Join pthreads upon completion
        //   As currently written, pthreads run indefinitely, 
        //   so execution will block here.
        // Threads will terminate when program is killed or receives
        // a SIGINT signal (Ctrl+C) 

        retval_int = pthread_join(pthread_msg_send, &retval_pthread);
        if(unlikely(retval_int != 0))
        {
                perror("main(...): pthread_join(pthread_msg_send, ...)");
                retval_main = 90;
                goto end;
        }

        retval_int = pthread_join(pthread_read_a2d, &retval_pthread);
        if(unlikely(retval_int != 0))
        {
                perror("main(...): pthread_join(pthread_read_a2d, ...)");
                retval_main = 100;
                goto end;
        }

        retval_int = close(fd_timer_msg_send);
        if(unlikely(retval_int == -1))
        {
                perror("main(...): close(fd_timer_msg_send)");
                retval_main = 110;
                goto end;
        }

        retval_int = close(fd_timer_read_a2d);
        if(unlikely(retval_int == -1))
        {
                perror("main(...): close(fd_timer_read_a2d)");
                retval_main = 120;
                goto end;
         }

end:
        return retval_main;
}
