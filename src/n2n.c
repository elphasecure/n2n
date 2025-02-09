/**
 * (C) 2007-21 - ntop.org and contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not see see <http://www.gnu.org/licenses/>
 *
 */

#include "n2n.h"

#include "sn_selection.h"

#include "minilzo.h"

#include <assert.h>


static const uint8_t broadcast_addr[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
static const uint8_t multicast_addr[6] = { 0x01, 0x00, 0x5E, 0x00, 0x00, 0x00 }; /* First 3 bytes are meaningful */
static const uint8_t ipv6_multicast_addr[6] = { 0x33, 0x33, 0x00, 0x00, 0x00, 0x00 }; /* First 2 bytes are meaningful */
static const n2n_mac_t null_mac = {0, 0, 0, 0, 0, 0};

/* ************************************** */

SOCKET open_socket (int local_port, int bind_any) {

    SOCKET sock_fd;
    struct sockaddr_in local_address;
    int sockopt;

    if((sock_fd = socket(PF_INET, SOCK_DGRAM, 0)) < 0) {
        traceEvent(TRACE_ERROR, "Unable to create socket [%s][%d]\n",
                   strerror(errno), sock_fd);
        return(-1);
    }

#ifndef WIN32
    /* fcntl(sock_fd, F_SETFL, O_NONBLOCK); */
#endif

    sockopt = 1;
    setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, (char *)&sockopt, sizeof(sockopt));

    memset(&local_address, 0, sizeof(local_address));
    local_address.sin_family = AF_INET;
    local_address.sin_port = htons(local_port);
    local_address.sin_addr.s_addr = htonl(bind_any ? INADDR_ANY : INADDR_LOOPBACK);

    if(bind(sock_fd,(struct sockaddr*) &local_address, sizeof(local_address)) == -1) {
        traceEvent(TRACE_ERROR, "Bind error on local port %u [%s]\n", local_port, strerror(errno));
        return(-1);
    }

    return(sock_fd);
}

static int traceLevel = 2 /* NORMAL */;
static int useSyslog = 0, syslog_opened = 0;
static FILE *traceFile = NULL;

int getTraceLevel () {

    return(traceLevel);
}

void setTraceLevel (int level) {

    traceLevel = level;
}

void setUseSyslog (int use_syslog) {

    useSyslog = use_syslog;
}

void setTraceFile (FILE *f) {

    traceFile = f;
}

void closeTraceFile () {

    if((traceFile != NULL) && (traceFile != stdout)) {
        fclose(traceFile);
    }
#ifndef WIN32
    if(useSyslog && syslog_opened) {
        closelog();
        syslog_opened = 0;
    }
#endif
}

#define N2N_TRACE_DATESIZE 32
void traceEvent (int eventTraceLevel, char* file, int line, char * format, ...) {

    va_list va_ap;

    if(traceFile == NULL) {
        traceFile = stdout;
    }

    if(eventTraceLevel <= traceLevel) {
        char buf[1024];
        char out_buf[1280];
        char theDate[N2N_TRACE_DATESIZE];
        char *extra_msg = "";
        time_t theTime = time(NULL);
        int i;

        /* We have two paths - one if we're logging, one if we aren't
         * Note that the no-log case is those systems which don't support it(WIN32),
         * those without the headers !defined(USE_SYSLOG)
         * those where it's parametrically off...
         */

        memset(buf, 0, sizeof(buf));
        strftime(theDate, N2N_TRACE_DATESIZE, "%d/%b/%Y %H:%M:%S", localtime(&theTime));

        va_start(va_ap, format);
        vsnprintf(buf, sizeof(buf) - 1, format, va_ap);
        va_end(va_ap);

        if(eventTraceLevel == 0 /* TRACE_ERROR */) {
            extra_msg = "ERROR: ";
        } else if(eventTraceLevel == 1 /* TRACE_WARNING */) {
            extra_msg = "WARNING: ";
        }

        while(buf[strlen(buf) - 1] == '\n') {
            buf[strlen(buf) - 1] = '\0';
        }

#ifndef WIN32
        if(useSyslog) {
            if(!syslog_opened) {
                openlog("n2n", LOG_PID, LOG_DAEMON);
                syslog_opened = 1;
            }

            snprintf(out_buf, sizeof(out_buf), "%s%s", extra_msg, buf);
            syslog(LOG_INFO, "%s", out_buf);
        } else {
            for(i = strlen(file) - 1; i > 0; i--) {
                if(file[i] == '/') {
                    i++;
                    break;
                }
            }
            snprintf(out_buf, sizeof(out_buf), "%s [%s:%d] %s%s", theDate, &file[i], line, extra_msg, buf);
            fprintf(traceFile, "%s\n", out_buf);
            fflush(traceFile);
        }
#else
        /* this is the WIN32 code */
        for(i = strlen(file) - 1; i > 0; i--) {
            if(file[i] == '\\') {
                i++;
                break;
            }
        }
        snprintf(out_buf, sizeof(out_buf), "%s [%s:%d] %s%s", theDate, &file[i], line, extra_msg, buf);
        fprintf(traceFile, "%s\n", out_buf);
        fflush(traceFile);
#endif
    }

}

/* *********************************************** */

/* addr should be in network order. Things are so much simpler that way. */
char* intoa (uint32_t /* host order */ addr, char* buf, uint16_t buf_len) {

    char *cp, *retStr;
    uint8_t byteval;
    int n;

    cp = &buf[buf_len];
    *--cp = '\0';

    n = 4;
    do {
        byteval = addr & 0xff;
        *--cp = byteval % 10 + '0';
        byteval /= 10;
        if(byteval > 0) {
            *--cp = byteval % 10 + '0';
            byteval /= 10;
            if(byteval > 0) {
                *--cp = byteval + '0';
            }
        }
        *--cp = '.';
        addr >>= 8;
    } while(--n > 0);

    /* Convert the string to lowercase */
    retStr = (char*)(cp + 1);

    return(retStr);
}


/** Convert subnet prefix bit length to host order subnet mask. */
uint32_t bitlen2mask (uint8_t bitlen) {

    uint8_t i;
    uint32_t mask = 0;

    for (i = 1; i <= bitlen; ++i) {
        mask |= 1 << (32 - i);
    }

    return mask;
}


/** Convert host order subnet mask to subnet prefix bit length. */
uint8_t mask2bitlen (uint32_t mask) {

    uint8_t i, bitlen = 0;

    for (i = 0; i < 32; ++i) {
        if((mask << i) & 0x80000000) {
            ++bitlen;
        } else {
            break;
        }
    }

    return bitlen;
}


/* *********************************************** */

char * macaddr_str (macstr_t buf,
                    const n2n_mac_t mac) {

    snprintf(buf, N2N_MACSTR_SIZE, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0] & 0xFF, mac[1] & 0xFF, mac[2] & 0xFF,
             mac[3] & 0xFF, mac[4] & 0xFF, mac[5] & 0xFF);

    return(buf);
}

/* *********************************************** */

/** Resolve the supernode IP address.
 *
 *  REVISIT: This is a really bad idea. The edge will block completely while the
 *  hostname resolution is performed. This could take 15 seconds.
 */
int supernode2sock (n2n_sock_t * sn, const n2n_sn_name_t addrIn) {

    n2n_sn_name_t addr;
    const char *supernode_host;
    int rv = 0;

    memcpy(addr, addrIn, N2N_EDGE_SN_HOST_SIZE);

    supernode_host = strtok(addr, ":");

    if(supernode_host) {
        in_addr_t sn_addr;
        char *supernode_port = strtok(NULL, ":");
        const struct addrinfo aihints = {0, PF_INET, 0, 0, 0, NULL, NULL, NULL};
        struct addrinfo * ainfo = NULL;
        int nameerr;

        if(supernode_port) {
            sn->port = atoi(supernode_port);
        } else {
            traceEvent(TRACE_WARNING, "Bad supernode parameter (-l <host:port>) %s %s:%s",
                       addr, supernode_host, supernode_port);
        }

        nameerr = getaddrinfo(supernode_host, NULL, &aihints, &ainfo);

        if(0 == nameerr) {
            struct sockaddr_in * saddr;

            /* ainfo s the head of a linked list if non-NULL. */
            if(ainfo && (PF_INET == ainfo->ai_family)) {
                /* It is definitely and IPv4 address -> sockaddr_in */
                saddr = (struct sockaddr_in *)ainfo->ai_addr;

                memcpy(sn->addr.v4, &(saddr->sin_addr.s_addr), IPV4_SIZE);
                sn->family = AF_INET;
            } else {
                /* Should only return IPv4 addresses due to aihints. */
                traceEvent(TRACE_WARNING, "Failed to resolve supernode IPv4 address for %s", supernode_host);
                rv = -1;
            }

            freeaddrinfo(ainfo); /* free everything allocated by getaddrinfo(). */
            ainfo = NULL;
        } else {
            traceEvent(TRACE_WARNING, "Failed to resolve supernode host %s, %d: %s", supernode_host, nameerr, gai_strerror(nameerr));
            rv = -2;
        }

    } else {
        traceEvent(TRACE_WARNING, "Wrong supernode parameter (-l <host:port>)");
        rv = -3;
    }

    return(rv);
}

/* ************************************** */

struct peer_info* add_sn_to_list_by_mac_or_sock (struct peer_info **sn_list, n2n_sock_t *sock, n2n_mac_t *mac, int *skip_add) {

    struct peer_info *scan, *tmp, *peer = NULL;

    if(memcmp(mac, null_mac, sizeof(n2n_mac_t)) != 0) { /* not zero MAC */
        HASH_FIND_PEER(*sn_list, mac, peer);
    }

    if(peer == NULL) { /* zero MAC, search by socket */
        HASH_ITER(hh, *sn_list, scan, tmp) {
            if(memcmp(&(scan->sock), sock, sizeof(n2n_sock_t)) == 0) {
                HASH_DEL(*sn_list, scan);
                memcpy(&(scan->mac_addr), mac, sizeof(n2n_mac_t));
                HASH_ADD_PEER(*sn_list, scan);
                peer = scan;
                break;
            }
        }

        if((peer == NULL) && (*skip_add == SN_ADD)) {
            peer = (struct peer_info*)calloc(1, sizeof(struct peer_info));
            if(peer) {
                sn_selection_criterion_default(&(peer->selection_criterion));
                memcpy(&(peer->sock), sock, sizeof(n2n_sock_t));
                memcpy(&(peer->mac_addr), mac, sizeof(n2n_mac_t));
                HASH_ADD_PEER(*sn_list, peer);
                *skip_add = SN_ADD_ADDED;
            }
        }
    }

    return peer;
}

/* ************************************************ */

uint8_t is_multi_broadcast (const uint8_t * dest_mac) {

    int is_broadcast = (memcmp(broadcast_addr, dest_mac, 6) == 0);
    int is_multicast = (memcmp(multicast_addr, dest_mac, 3) == 0);
    int is_ipv6_multicast = (memcmp(ipv6_multicast_addr, dest_mac, 2) == 0);

    return is_broadcast || is_multicast || is_ipv6_multicast;
}

/* http://www.faqs.org/rfcs/rfc908.html */


/* *********************************************** */

char* msg_type2str (uint16_t msg_type) {

    switch(msg_type) {
        case MSG_TYPE_REGISTER: return("MSG_TYPE_REGISTER");
        case MSG_TYPE_DEREGISTER: return("MSG_TYPE_DEREGISTER");
        case MSG_TYPE_PACKET: return("MSG_TYPE_PACKET");
        case MSG_TYPE_REGISTER_ACK: return("MSG_TYPE_REGISTER_ACK");
        case MSG_TYPE_REGISTER_SUPER: return("MSG_TYPE_REGISTER_SUPER");
        case MSG_TYPE_REGISTER_SUPER_ACK: return("MSG_TYPE_REGISTER_SUPER_ACK");
        case MSG_TYPE_REGISTER_SUPER_NAK: return("MSG_TYPE_REGISTER_SUPER_NAK");
        case MSG_TYPE_FEDERATION: return("MSG_TYPE_FEDERATION");
        default: return("???");
    }

    return("???");
}

/* *********************************************** */

void hexdump (const uint8_t *buf, size_t len) {

    size_t i;

    if(0 == len) {
        return;
    }

    printf("-----------------------------------------------\n");
    for(i = 0; i < len; i++) {
        if((i > 0) && ((i % 16) == 0)) {
            printf("\n");
        }
        printf("%02X ", buf[i] & 0xFF);
    }
    printf("\n");
    printf("-----------------------------------------------\n");
}


/* *********************************************** */

void print_n2n_version () {

    printf("Welcome to n2n v.%s for %s\n"
           "Built on %s\n"
           "Copyright 2007-2020 - ntop.org and contributors\n\n",
           GIT_RELEASE, PACKAGE_OSNAME, PACKAGE_BUILDDATE);
}

/* *********************************************** */

size_t purge_expired_registrations (struct peer_info ** peer_list, time_t* p_last_purge, int timeout) {

    time_t now = time(NULL);
    size_t num_reg = 0;

    if((now - (*p_last_purge)) < timeout) {
        return 0;
    }

    traceEvent(TRACE_DEBUG, "Purging old registrations");

    num_reg = purge_peer_list(peer_list, now - REGISTRATION_TIMEOUT);

    (*p_last_purge) = now;
    traceEvent(TRACE_DEBUG, "Remove %ld registrations", num_reg);

    return num_reg;
}

/** Purge old items from the peer_list and return the number of items that were removed. */
size_t purge_peer_list (struct peer_info ** peer_list,
                        time_t purge_before) {

    struct peer_info *scan, *tmp;
    size_t retval = 0;

    HASH_ITER(hh, *peer_list, scan, tmp) {
        if((scan->purgeable == SN_PURGEABLE) && (scan->last_seen < purge_before)) {
            HASH_DEL(*peer_list, scan);
            retval++;
            free(scan);
        }
    }

    return retval;
}

/** Purge all items from the peer_list and return the number of items that were removed. */
size_t clear_peer_list (struct peer_info ** peer_list) {

    struct peer_info *scan, *tmp;
    size_t retval = 0;

    HASH_ITER(hh, *peer_list, scan, tmp) {
        HASH_DEL(*peer_list, scan);
        retval++;
        free(scan);
    }

    return retval;
}

static uint8_t hex2byte (const char * s) {

    char tmp[3];
    tmp[0] = s[0];
    tmp[1] = s[1];
    tmp[2] = 0; /* NULL term */

    return((uint8_t)strtol(tmp, NULL, 16));
}

extern int str2mac (uint8_t * outmac /* 6 bytes */, const char * s) {

    size_t i;

    /* break it down as one case for the first "HH", the 5 x through loop for
     * each ":HH" where HH is a two hex nibbles in ASCII. */

    *outmac = hex2byte(s);
    ++outmac;
    s += 2; /* don't skip colon yet - helps generalise loop. */

    for(i = 1; i < 6; ++i) {
        s += 1;
        *outmac = hex2byte(s);
        ++outmac;
        s += 2;
    }

    return 0; /* ok */
}

extern char * sock_to_cstr (n2n_sock_str_t out,
                            const n2n_sock_t * sock) {

    if(NULL == out) {
        return NULL;
    }
    memset(out, 0, N2N_SOCKBUF_SIZE);

    if(AF_INET6 == sock->family) {
        /* INET6 not written yet */
        snprintf(out, N2N_SOCKBUF_SIZE, "XXXX:%hu", sock->port);
        return out;
    } else {
        const uint8_t * a = sock->addr.v4;

        snprintf(out, N2N_SOCKBUF_SIZE, "%hu.%hu.%hu.%hu:%hu",
                 (unsigned short)(a[0] & 0xff),
                 (unsigned short)(a[1] & 0xff),
                 (unsigned short)(a[2] & 0xff),
                 (unsigned short)(a[3] & 0xff),
                 (unsigned short)sock->port);
        return out;
    }
}

char *ip_subnet_to_str (dec_ip_bit_str_t buf, const n2n_ip_subnet_t *ipaddr) {

    snprintf(buf, sizeof(dec_ip_bit_str_t), "%hhu.%hhu.%hhu.%hhu/%hhu",
             (uint8_t) ((ipaddr->net_addr >> 24) & 0xFF),
             (uint8_t) ((ipaddr->net_addr >> 16) & 0xFF),
             (uint8_t) ((ipaddr->net_addr >> 8) & 0xFF),
             (uint8_t) (ipaddr->net_addr & 0xFF),
             ipaddr->net_bitlen);

    return buf;
}


/* @return 1 if the two sockets are equivalent. */
int sock_equal (const n2n_sock_t * a,
                const n2n_sock_t * b) {

    if(a->port != b->port) {
        return(0);
    }

    if(a->family != b->family) {
        return(0);
    }

    switch(a->family) {
        case AF_INET:
            if(memcmp(a->addr.v4, b->addr.v4, IPV4_SIZE)) {
                return(0);
            }
            break;

        default:
            if(memcmp(a->addr.v6, b->addr.v6, IPV6_SIZE)) {
                return(0);
            }
            break;
    }

    /* equal */
    return(1);
}

/* *********************************************** */

#if defined(WIN32)
int gettimeofday (struct timeval *tp, void *tzp) {

    time_t clock;
    struct tm tm;
    SYSTEMTIME wtm;

    GetLocalTime(&wtm);
    tm.tm_year = wtm.wYear - 1900;
    tm.tm_mon = wtm.wMonth - 1;
    tm.tm_mday = wtm.wDay;
    tm.tm_hour = wtm.wHour;
    tm.tm_min = wtm.wMinute;
    tm.tm_sec = wtm.wSecond;
    tm.tm_isdst = -1;
    clock = mktime(&tm);
    tp->tv_sec = clock;
    tp->tv_usec = wtm.wMilliseconds * 1000;

    return 0;
}
#endif


// returns a time stamp for use with replay protection
uint64_t time_stamp (void) {

    struct timeval tod;
    uint64_t micro_seconds;

    gettimeofday(&tod, NULL);

    // (roughly) calculate the microseconds since 1970, note that the 8 bits between time stamp and flags remain unset
    micro_seconds = ((uint64_t)(tod.tv_sec) << 32) + ((uint64_t)tod.tv_usec << 12);
    // more exact but more costly due to the multiplication:
    // micro_seconds = ((uint64_t)(tod.tv_sec) * 1000000ULL + tod.tv_usec) << 12;

    // note that the lower 4 bits remain unset (flags, for later use)

    return micro_seconds;
}


// returns an initial time stamp for use with replay protection
uint64_t initial_time_stamp (void) {

    return time_stamp() - TIME_STAMP_FRAME;
}


// checks if a provided time stamp is consistent with current time and previously valid time stamps
// and, in case of validity, updates the "last valid time stamp"
int time_stamp_verify_and_update (uint64_t stamp, uint64_t *previous_stamp, int allow_jitter) {

    int64_t diff; // do not change to unsigned

    // clear any incoming flags (their handling is not suppoted yet)
    stamp = (stamp >> 4) << 4;

    // are the 8 bits between time stamp and flags reset? this relies on flags being cleared above
    if(stamp << 52) {
        traceEvent(TRACE_DEBUG, "time_stamp_verify_and_update found a timestamp with middle bits set.");
        return 0; // failure
    }

    // is it around current time (+/- allowed deviation TIME_STAMP_FRAME)?
    diff = stamp - time_stamp();
    // abs()
    diff = (diff < 0 ? -diff : diff);
    if(diff >= TIME_STAMP_FRAME) {
        traceEvent(TRACE_DEBUG, "time_stamp_verify_and_update found a timestamp out of allowed frame.");
        return 0; // failure
    }

    // if applicable: is it higher than previous time stamp (including allowed deviation of TIME_STAMP_JITTER)?
    if(NULL != previous_stamp) {
        diff = stamp - *previous_stamp;
        if(allow_jitter) {
            diff += TIME_STAMP_JITTER;
        }

        if(diff <= 0) {
            traceEvent(TRACE_DEBUG, "time_stamp_verify_and_update found a timestamp too old compared to previous.");
            return 0; // failure
        }
        // for not allowing to exploit the allowed TIME_STAMP_JITTER to "turn the clock backwards",
        // set the higher of the values
        *previous_stamp = (stamp > *previous_stamp ? stamp : *previous_stamp);
    }

    return 1; // success
}
