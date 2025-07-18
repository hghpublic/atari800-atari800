/*
* netsio.c - NetSIO interface for FujiNet-PC <-> Atari800 Emulator
*
* fujinet_rx_thread receives from FujiNet-PC, responds to pings/alives,
* queues complete packets to emulator
*
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <time.h>
#include "netsio.h"
#include "log.h"
#include "pia.h" /* For toggling PROC & INT */


#ifdef DEBUG
static char *buf_to_hex(const uint8_t *buf, size_t offset, size_t len);
#endif /* DEBUG */
static void send_block_to_fujinet(const uint8_t *block, size_t len);

/* Flag to know when netsio is enabled */
volatile int netsio_enabled = 0;
/* Holds sync to fujinet-pc incremented number */
uint8_t netsio_sync_num = 0;
/* if we have heard from fujinet-pc or not */
int fujinet_known = 0;
/* wait for fujinet sync if true */
volatile int netsio_sync_wait = 0;
/* true if cmd line pulled */
int netsio_cmd_state = 0;
/* data frame size for SIO write commands */
volatile int netsio_next_write_size = 0;

/* FIFO pipe: fds0: FujiNet->emulator */
int fds0[2];

/* UDP socket for NetSIO and return address holder */
static int sockfd = -1;
static struct sockaddr_storage fujinet_addr;
static socklen_t fujinet_addr_len = sizeof(fujinet_addr);

/* Thread declaration */
static void *fujinet_rx_thread(void *arg);

static void millisleep(unsigned int ms)
{
    struct timespec req, rem;
    req.tv_sec  = ms / 1000;
    req.tv_nsec = (long)(ms % 1000) * 1000000L;

    while (nanosleep(&req, &rem) == -1 && errno == EINTR) {
        req = rem;
    }
}

#ifdef DEBUG
char *buf_to_hex(const uint8_t *buf, size_t offset, size_t len) {
    /* each byte takes "XX " == 3 chars, +1 for trailing NUL */
    size_t needed = len * 3 + 1;
    char *s = malloc(needed);
    char *p;
    size_t i = 0;
    if (!s) return NULL;
    p = s;
    for (i = 0; i < len; i++)
    {
        sprintf(p, "%02X ", buf[offset + i]);
        p += 3;
    }
    if (len)
        p[-1] = '\0';
    else
        *p = '\0';
    return s;
}
#endif

/* write data to emulator FIFO (fujinet_rx_thread) */
static void enqueue_to_emulator(const uint8_t *pkt, size_t len) {
    ssize_t n;
    while (len > 0)
    {
        n = write(fds0[1], pkt, len);
        if (n < 0)
        {
            if (errno == EINTR) continue;
#ifdef DEBUG
            Log_print("netsio: write to emulator FIFO");
#endif
            /*exit(1);*/
        }
        pkt += n;
        len -= n;
    }
}

/* send a packet to FujiNet socket */
static void send_to_fujinet(const uint8_t *pkt, size_t len) {
    ssize_t n;
    socklen_t addr_len;
    int flags = 0;
#ifdef DEBUG2
    size_t buf_size = len * 3 + 1;
    char *hexdump = malloc(buf_size);
    size_t pos = 0, i = 0;
#endif
    /* if we never received a ping from FujiNet or we have no address to reply to */
    if (!fujinet_known || fujinet_addr.ss_family != AF_INET)
    {
#ifdef DEBUG
        Log_print("netsio: can't send_to_fujinet, no address");
#endif
        return;
    }
    
    /*
     * Using the correct size for IPv4 addresses ensures compatibility with both Linux and macOS
     */
    addr_len = sizeof(struct sockaddr_in);
    
    /*
     * On macOS, SIGPIPE is typically handled using the SO_NOSIGPIPE socket option
     * instead, but we're just avoiding the flag entirely for simplicity
     */
#if defined(MSG_NOSIGNAL) && !defined(__APPLE__)
    /* Only use MSG_NOSIGNAL on Linux and other platforms that support it */
    flags |= MSG_NOSIGNAL;
#endif

    n = sendto(
        sockfd,
        pkt, len, flags,
        (struct sockaddr *)&fujinet_addr,
        addr_len
    );
    if (n < 0)
    {
        if (errno == EINTR)
        {
            /* transient, try once more */
            n = sendto(
                sockfd,
                pkt, len, flags,
                (struct sockaddr *)&fujinet_addr,
                addr_len
            );
        }
        if (n < 0)
        {
#ifdef DEBUG
            Log_print("netsio: sendto fn failed: %d", errno);
#endif
            return;
        }
    }
    else if ((size_t)n != len)
    {
#ifdef DEBUG
        Log_print("netsio: partial send (%zd of %zu bytes)", n, len);
#endif
        return;
    }

#ifdef DEBUG2
    /* build a hex string: each byte "XX " */
    for (i = 0; i < len; i++) {
        /* snprintf returns number of chars (excluding trailing NUL) */
        int written = snprintf(&hexdump[pos], buf_size - pos, "%02X ", pkt[i]);
        if (written < 0 || (size_t)written >= buf_size - pos) {
            break;
        }
        pos += written;
    }
    hexdump[pos] = '\0';
    Log_print("netsio: send: %zu bytes → %s", len, hexdump);
    free(hexdump);
#endif
}

/* Send up to 512 bytes as a DATA_BLOCK packet */
static void send_block_to_fujinet(const uint8_t *block, size_t len) {
    uint8_t packet[512 + 2];

    if (len == 0 || len > 512) return;  /* sanity check */
    packet[0] = NETSIO_DATA_BLOCK;
    memcpy(&packet[1], block, len);
    /* Pad the end with a junk byte or FN-PC won't accept the packet */
    packet[1 + len] = 0xFF;
    send_to_fujinet(packet, len + 2);
}

/* Initialize NetSIO:
*   - connect to FujiNet socket
*   - create FIFO
*   - spawn the thread
*/
int netsio_init(uint16_t port) {
    struct sockaddr_in addr;
    pthread_t rx_thread;
    int broadcast = 1;

    /* create emulator <-> netsio FIFOs */
    if (pipe(fds0) < 0)
    {
#ifdef DEBUG
        Log_print("netsio: pipe creation error");
#endif
        return -1;
    }

    /* fcntl(fds0[0], F_SETFL, O_NONBLOCK); */

    /* connect socket to FujiNet */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
    {
#ifdef DEBUG
        Log_print("netsio: socket error");
#endif
        return -1;
    }
    /* Fill in the structure with port number, any IP */
    memset(&addr, 0, sizeof(addr));
    
    /*
     * Linux ignores this field since it doesn't exist in the Linux socket API
     */
#ifdef __APPLE__
    addr.sin_len = sizeof(addr); /* Only needed on macOS/BSD systems */
#endif
    
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    
    /* 
     * Enable broadcast on the socket - required on all platforms
     * This is needed for sending broadcast packets to FujiNet
     * Works the same way on both Linux and macOS
     */
    if (setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) < 0)
    {
#ifdef DEBUG
        Log_print("netsio setsockopt SO_BROADCAST");
#endif
    }

    /* Bind to the socket on requested port */
    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
#ifdef DEBUG
        Log_print("netsio bind socket error");
#endif
        close(sockfd);
        return -1;
    }

    /* spawn receiver thread */
    if (pthread_create(&rx_thread, NULL, fujinet_rx_thread, NULL) != 0)
    {
#ifdef DEBUG
        Log_print("netsio: pthread_create rx");
#endif
        return -1;
    }
    pthread_detach(rx_thread);

    return 0;
}

/* Called when a command frame with sync response is sent to FujiNet */
void netsio_wait_for_sync(void)
{
    int ticker = 0;
    while (netsio_sync_wait)
    {
#ifdef DEBUG
        Log_print("netsio: waiting for sync response - %d", ticker);
#endif
        millisleep(5);
        if (ticker > 7)
        {
            netsio_sync_wait = 0;
            break;
        }
        ticker++;
    }
}

/* Return number of bytes waiting from FujiNet to emulator */
int netsio_available(void) {
    int avail = 0;
    if (fds0[0] >= 0)
    {
        if (ioctl(fds0[0], FIONREAD, &avail) < 0)
        {
#ifdef DEBUG
            Log_print("netsio_avail: ioctl error");
#endif
            return -1;
        }
    }
    return avail;
}

/* COMMAND ON */
int netsio_cmd_on(void)
{
    uint8_t p;
    p = NETSIO_COMMAND_ON;
#ifdef DEBUG
    Log_print("netsio: CMD ON");
#endif
    netsio_cmd_state = 1;
    send_to_fujinet(&p, 1);
    return 0;
}

/* COMMAND OFF */
int netsio_cmd_off(void)
{
    uint8_t p;
    p = NETSIO_COMMAND_OFF;
#ifdef DEBUG
    Log_print("netsio: CMD OFF");
#endif
    send_to_fujinet(&p, 1);
    return 0;
}

/* COMMAND OFF with SYNC */
int netsio_cmd_off_sync(void)
{
    uint8_t p[2];
    p[0] = NETSIO_COMMAND_OFF_SYNC;
    netsio_sync_num++;
    p[1] = netsio_sync_num;
#ifdef DEBUG
    Log_print("netsio: CMD OFF SYNC");
#endif
    send_to_fujinet(p, sizeof(p));
    netsio_sync_wait = 1; /* pause emulation until we hear back or timeout */
    return 0;
}

/* Toggle Command Line */
void netsio_toggle_cmd(int v)
{
    if (!v)
        netsio_cmd_off_sync();
    else
        netsio_cmd_on();
}

/* The emulator calls this to send a data byte out to FujiNet */
int netsio_send_byte(uint8_t b) {
    uint8_t pkt[2];
    pkt[0] = NETSIO_DATA_BYTE;
    pkt[1] = b;
#ifdef DEBUG
    Log_print("netsio: send byte: %02X", b);
#endif
    send_to_fujinet(pkt, 2);
    return 0;
}

/* The emulator calls this to send a data block out to FujiNet */
int netsio_send_block(const uint8_t *block, ssize_t len) {
    send_block_to_fujinet(block, len);
#ifdef DEBUG
    Log_print("netsio: send block, %i bytes:\n  %s", len, buf_to_hex(block, 0, len));
#endif
    return 0;
}

/* DATA BYTE with SYNC */
int netsio_send_byte_sync(uint8_t b)
{
    uint8_t p[3];
    p[0] = NETSIO_DATA_BYTE_SYNC;
    p[1] = b;
    netsio_sync_num++;
    p[2] = netsio_sync_num;
#ifdef DEBUG
    Log_print("netsio: send byte: 0x%02X sync: %d", b, netsio_sync_num);
#endif
    send_to_fujinet(p, sizeof(p));
    netsio_sync_wait = 1; /* pause emulation until we hear back or timeout s*/
    return 0;
}

/* The emulator calls this to receive a data byte from FujiNet */
int netsio_recv_byte(uint8_t *b) {
    ssize_t n = read(fds0[0], b, 1);
    if (n < 0)
    {
        if (errno == EINTR) return netsio_recv_byte(b);
#ifdef DEBUG
        Log_print("netsio: read from rx FIFO");
#endif
        return -1;
    }
    if (n == 0)
        return -1; /* FIFO closed? */
#ifdef DEBUG2
    Log_print("netsio: read to emu: %02X", (unsigned)*b);
#endif
    return 0;
}

/* Send netsio COLD reset 0xFF */
int netsio_cold_reset(void) {
    uint8_t pkt = 0xFF;
#ifdef DEBUG
    Log_print("netsio: cold reset");
#endif
    send_to_fujinet(&pkt, 1);
    return 0;
}

/* Send netsio WARM reset 0xFE */
int netsio_warm_reset(void) {
    uint8_t pkt = 0xFE;
#ifdef DEBUG
    Log_print("netsio: warm reset");
#endif
    send_to_fujinet(&pkt, 1);
    return 0;
}

/* Send a test command frame to fujinet-pc */
void netsio_test_cmd(void)
{
    uint8_t p[6] = { 0x70, 0xE8, 0x00, 0x00, 0x59 }; /* Send fujidev get adapter config request */
    netsio_cmd_on(); /* Turn on CMD */
    send_block_to_fujinet(p, sizeof(p));
    netsio_cmd_off_sync(); /* Turn off CMD */
}

/* Thread: receive from FujiNet socket (one packet == one command) */
static void *fujinet_rx_thread(void *arg) {
    uint8_t buf[4096];
    uint8_t cmd;
    ssize_t n;

    for (;;)
    {
        /* 
         * Always initialize with full sockaddr_storage size for receiving
         * This works on both Linux and macOS - we need full size for first connect
         */
        fujinet_addr_len = sizeof(fujinet_addr);
        
        /*
         * If we've already established communication and know it's IPv4, we set the
         * appropriate length in the sin_len field to ensure proper socket operation on macOS.
         */
#ifdef __APPLE__
        if (fujinet_addr.ss_family == AF_INET) {
            /* Only on macOS/BSD: Set sin_len for existing IPv4 connection */
            struct sockaddr_in *addr_in = (struct sockaddr_in *)&fujinet_addr;
            addr_in->sin_len = sizeof(struct sockaddr_in);
        }
#endif
        
        n = recvfrom(sockfd,
                             buf, sizeof(buf),
                             0,
                             (struct sockaddr *)&fujinet_addr,
                             &fujinet_addr_len);

        if (n <= 0)
        {
#ifdef DEBUG
            Log_print("netsio: recv");
#endif
            continue;
        }
        fujinet_known = 1;
        
        /* Update the address length to the correct size for future sends */
        if (fujinet_addr.ss_family == AF_INET) {
            /* For IPv4, use sizeof sockaddr_in */
            fujinet_addr_len = sizeof(struct sockaddr_in);
        }

        /* Every packet must be at least one byte (the command) */
        if (n < 1)
        {
#ifdef DEBUG
            Log_print("netsio: empty packet");
#endif
            continue;
        }

        cmd = buf[0];

        switch (cmd)
        {
            case NETSIO_PING_REQUEST:
            {
                uint8_t r = NETSIO_PING_RESPONSE;
                send_to_fujinet(&r, 1);
#ifdef DEBUG
                Log_print("netsio: recv: PING→PONG");
#endif
                break;
            }

            case NETSIO_DEVICE_CONNECTED: 
            {
#ifdef DEBUG
                Log_print("netsio: recv: device connected");
#endif
                netsio_enabled = 1;
                break;
            }

            case NETSIO_DEVICE_DISCONNECTED:
            {
#ifdef DEBUG
                Log_print("netsio: recv: device disconnected");
#endif
                netsio_enabled = 0;
                break;
            }
            
            case NETSIO_ALIVE_REQUEST:
            {
                uint8_t r = NETSIO_ALIVE_RESPONSE;
                send_to_fujinet(&r, 1);
#ifdef DEBUG2
                Log_print("netsio: recv: IT'S ALIVE!");
#endif
                break;
            }

            case NETSIO_CREDIT_STATUS:
            {
                uint8_t reply[2];
                /* packet should be 2 bytes long */
                if (n < 2)
                {
#ifdef DEBUG
                    Log_print("netsio: recv: CREDIT_STATUS packet too short (%zd)", n);
#endif
                }
                reply[0] = NETSIO_CREDIT_UPDATE;
                reply[1] = 3;
                send_to_fujinet(reply, sizeof(reply));
#ifdef DEBUG
                Log_print("netsio: recv: credit status & response");
#endif
                break;
            }

            case NETSIO_SPEED_CHANGE:
            {
                /* packet: [cmd][baud32le] */
                uint32_t baud;
                if (n < 5)
                {
#ifdef DEBUG
                    Log_print("netsio: recv: SPEED_CHANGE packet too short (%zd)", n);
#endif
                    break;
                }
                baud  = (uint32_t)buf[1];
                baud |= (uint32_t)buf[2] <<  8;
                baud |= (uint32_t)buf[3] << 16;
                baud |= (uint32_t)buf[4] << 24;
#ifdef DEBUG
                Log_print("netsio: recv: requested baud rate %u", baud);
#endif
                send_to_fujinet(buf, 5); /* echo back */
                break;
            }

            case NETSIO_SYNC_RESPONSE:
            {
                /* packet: [cmd][sync#][ack_type][ack_byte][write_lo][write_hi] */
                uint8_t resp_sync, ack_type, ack_byte, write_size;

                if (n < 6)
                {
#ifdef DEBUG
                    Log_print("netsio: recv: SYNC_RESPONSE too short (%zd)", n);
#endif
                    break;
                }
                resp_sync  = buf[1];
                ack_type   = buf[2];
                ack_byte   = buf[3];
                write_size = buf[4] | (uint16_t)buf[5] << 8;

                if (resp_sync != netsio_sync_num)
                {
#ifdef DEBUG
                    Log_print("netsio: recv: sync-response: got %u, want %u", resp_sync, netsio_sync_num);
#endif
                }
                else
                {
                    if (ack_type == 0)
                    {
#ifdef DEBUG
                        Log_print("netsio: recv: sync %u NAK, dropping", resp_sync);
#endif
                    }
                    else if (ack_type == 1)
                    {
                        netsio_next_write_size = write_size;
#ifdef DEBUG
                        Log_print("netsio: recv: sync %u ACK byte=0x%02X  write_size=0x%04X", resp_sync, ack_byte, write_size);
#endif
                        enqueue_to_emulator(&ack_byte, 1);
                    }
                    else
                    {
#ifdef DEBUG
                        Log_print("netsio: recv: sync %u unknown ack_type %u", resp_sync, ack_type);
#endif
                    }
                }
                netsio_sync_wait = 0; /* continue emulation */
                break;
            }

            /* set_CA1 */
            case NETSIO_PROCEED_ON:
            {
                break;
            }
            case NETSIO_PROCEED_OFF:
            {
                break;
            }

            /* set_CB1 */
            case NETSIO_INTERRUPT_ON:
            {
                break;
            }
            case NETSIO_INTERRUPT_OFF:
            {
                break;
            }

            case NETSIO_DATA_BYTE:
            {
                /* packet: [cmd][data] */
                uint8_t data;
                if (n < 2)
                {
#ifdef DEBUG
                    Log_print("netsio: recv: DATA_BYTE too short (%zd)", n);
#endif
                    break;
                }
                data = buf[1];
#ifdef DEBUG
                Log_print("netsio: recv: data byte: 0x%02X", data);
#endif
                enqueue_to_emulator(&data, 1);
                break;
            }

            case NETSIO_DATA_BLOCK:
            {
                /* packet: [cmd][payload...] */
                size_t payload_len;
                if (n < 2)
                {
#ifdef DEBUG
                    Log_print("netsio: recv: data block too short (%zd)", n);
#endif
                    break;
                }
                /* payload length is everything after the command byte */
                payload_len = n - 1;
#ifdef DEBUG
                Log_print("netsio: recv: data block %zu bytes:\n  %s", payload_len, buf_to_hex(buf, 1, payload_len));
#endif
                /* forward only buf[1]..buf[n-1] */
                enqueue_to_emulator(buf + 1, payload_len);
                break;
            }            

            default:
            {
#ifdef DEBUG
                Log_print("netsio: recv: unknown cmd 0x%02X, length %zd", cmd, n);
#endif
                break;
            }
        }
    }
    return NULL;
}
