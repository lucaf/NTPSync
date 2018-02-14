//
//  NtpSync.c
//
//  Created by luca on 15/06/2012.
//
//  Note: this actually implements Cristian Synchronisation Algorithm on top of NTP.
//  The precision is set to be below 1 ms and clock is slewed over 1 second after
//  the offset has been updated.
//  Running in user mode (low priority), the thread which actually implements the
//  the sync procedure could be interrupted in any moment. To avoid that this
//  affects the measurement, the delay between a send and a receive is taken in
//  account a evaluated as an index of reliability of the measurement.
//
//  Build:
// /Developer/usr/bin/clang -shared -arch i386 -arch x86_64 -isysroot /Developer/SDKs/MacOSX10.6.sdk -fpic DebugUtil.c NtpSync.c -lc -lpthread -framework CoreServices -o libNtpSync.dylib

#include <sys/time.h>
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include "ByteOrder.h"
#include "UdpConn.h"
#include "NtpSync.h"

#define DEBUG_BASIC     0x01
#define DEBUG_MEDIUM    0x02
#define DEBUG_DEEP      0x04

#define DEBUG_SWITCH    DEBUG_BASIC + DEBUG_MEDIUM + DEBUG_DEEP
#include "DebugUtil.h"

#define NTPSYNC_HEADER   "NTP-SYNCHRONIZER"
#define NTPSYNC_DBG(fmt, ...) eprintf(NTPSYNC_HEADER, fmt, __VA_ARGS__)

// -- Very basic stuff on time

#ifdef __APPLE__
    #include <Carbon/Carbon.h>
    #include <CoreAudio/CoreAudio.h>
    #define GETSECS() (((double)UnsignedWideToUInt64(AbsoluteToNanoseconds(UpTime()))) / 1e9)
#endif

#ifdef __linux__
    #include <time.h>
    #include <assert.h>

    #ifdef CLOCK_MONOTONIC_RAW
        #define _CLOCK_TYPE CLOCK_MONOTONIC_RAW
    #else
        #define _CLOCK_TYPE CLOCK_MONOTONIC
    #endif

    #define GETSECS() ({ \
        struct timespec tp; \
        clock_gettime(_CLOCK_TYPE, &tp); \
        (double)tp.tv_sec + (double)tp.tv_nsec/1e9; \
    })
#endif

//----- NTP synchronisation (based on rfc5905)

// NTP macros
#define VERSION         4
#define MAXSTRAT        16
#define MAXDISP         16
#define MINPOLL         6
#define NOSYNC          0x3
#define	PHI             15e-6	// % frequency tolerance (15 PPM)
#define CKPRECISION     -18
#define JAN_1970        2208988800UL // 1970 - 1900 in secs

// Modes
#define M_RSVD   0 // reserved
#define M_SACT   1 // symmetric active
#define M_PASV   2 // symmetric passive
#define M_CLNT   3 // client
#define M_SERV   4 // server
#define M_BCST   5 // broacast server
#define M_BCLN   6 // broadcast client

typedef int64_t tstamp;
typedef uint16_t tdist;

typedef struct {
    uint32_t lvmspp;        // LI VN Mode Stratum Poll Precision
    int32_t rootdelay;      // total round trip delay to the reference clock
    int32_t rootdisp;       // total dispersion to the reference clock
    int32_t refid;
    tstamp reference_ts;
    tstamp origin_ts;
    tstamp receive_ts;
    tstamp transmit_ts;
} __attribute__((packed)) tNtpPkt;


// general macros
#ifdef MAX
    #undef MAX
#endif

#ifdef MIN
    #undef MIN
#endif

#define TWO_E32             0x100000000L
#define TWO_E16             0x10000L
#define MAX(a,b)            ((a) < (b) ? (b) : (a))
#define MIN(a,b)            ((a) < (b) ? (a) : (b))
#define ABS(a)              ((a) < 0 ? -(a) : (a))
#define	LOG2D(a)            ((a) < 0 ? 1. / (1L << -(a)) : 1L << (a))
#define	FP2D(a)             ((double)(a) / TWO_E16)     /* NTP short */
#define	D2FP(a)             ((tdist)((a) * TWO_E16))
#define LFP2D(a)            ((double)(a) / TWO_E32)
#define D2LFP(a)            ((tstamp)((a) * TWO_E32))
#define U2LFP(a)            (((unsigned long long)((a).tv_sec + JAN_1970) << 32) + (unsigned long long)((a).tv_usec / 1e6 * TWO_E32))
#define LFP70(a)            ((uint64_t)(a) - ((uint64_t)JAN_1970 << 32))
#define LFP00(a)            ((uint64_t)(a) + ((uint64_t)JAN_1970 << 32))

// lvmspp field access macros
#define LI(p)               ((p)->lvmspp  >> 30)
#define VN(p)               (((p)->lvmspp >> 27) & 0x07)
#define MODE(p)             (((p)->lvmspp >> 24) & 0x07)
#define STRATUM(p)          (((p)->lvmspp >> 16) & 0xFF)
#define POLL(p)             (((p)->lvmspp >> 8) & 0xFF)
#define PRECISION(p)        ((p)->lvmspp  & 0xFF)

#define LI_SET(p,v)         (p)->lvmspp |= (((uint32_t)(v & 0x03)) << 30)
#define VN_SET(p,v)         (p)->lvmspp |= (((uint32_t)(v & 0x07)) << 27)
#define MODE_SET(p,v)       (p)->lvmspp |= (((uint32_t)(v & 0x07)) << 24)
#define STRATUM_SET(p,v)    (p)->lvmspp |= (((uint32_t)(v & 0xFF)) << 16)
#define POLL_SET(p,v)       (p)->lvmspp |= (((uint32_t)(v & 0xFF)) << 8)
#define PRECISION_SET(p,v)  (p)->lvmspp |= (uint32_t)(v & 0xFF)

#define NTP_PACKET_SIZE     sizeof(tNtpPkt)

typedef struct {
    tstamp tzero_ntp_wck;   // remote ntp timestamp in ntp format
    double tzero_wck;       // remote ntp timestamp
    double tzero_sys;       // local system clock
    double tsync_sys;       // local system clock
    double offset;          // absolute offset (incremented abrupbtely of ofs_rel)
    double slewed_offset;   // absolute slewed offset (slowly increased to cover the gap of ofs_rel)
    double delay;           // RTT value for the correspondent ofs_rel one
    double ofs_rel;         // relative offset (intra adjustments)
    double ofs_rel_max;     // after the first ajustement
    double ofs_rel_min;     // after the first ajustement
    int adjustements;
} tTime;

#define LOC_OFS(t, now)         ((now) - (t)->tzero_sys + (t)->offset)
#define LOC_2_UNIX(t, l)        ((t)->tzero_wck + LOC_OFS(t, l))
#define LOC_2_NTP(t, l)         ((t)->tzero_ntp_wck + D2LFP(LOC_OFS(t, l)))
#define UNIX_TIME(t)            LOC_2_UNIX(t, GETSECS())
#define NTP_TIME(t)             LOC_2_NTP(t, GETSECS())

#define SLEWED_LOC_OFS(t, now)  ((now) - (t)->tzero_sys + (t)->slewed_offset)
#define SLEWED_LOC_2_UNIX(t, l) ((t)->tzero_wck + SLEWED_LOC_OFS(t, l))
#define SLEWED_LOC_2_NTP(t, l)  ((t)->tzero_ntp_wck + D2LFP(SLEWED_LOC_OFS(t, l)))
#define SLEWED_UNIX_TIME(t)     SLEWED_LOC_2_UNIX(t, GETSECS())
#define SLEWED_NTP_TIME(t)      SLEWED_LOC_2_NTP(t, GETSECS())

typedef struct {
    double send_ts[2];
    double recv_ts[2];
    double offset;
    double delay;
    double dispersion;
} tTimeStats;

typedef struct {
    tTime time;
    int comm;               // udp communication socket
    tstamp ntp_start_time;
    double start_time;
    double max_offset;      // maximum tolerated offset [seconds]
    int inter_sync_delay;   // the time in between one synch and the following [ms]
    int inited:1;
    int synchronised:1;
    int stop:1;
    eNtpSyncError error;
    tCbOnErr cb_err;
    void *cb_err_prm;
} tNtpTime;

#define NTP_PKT_BUF_SZ 8

#define SET_NTP_PACKET(p) do { \
    (p)->lvmspp = 0;  \
    LI_SET(p, NOSYNC);                      /* clock unsynchronized */ \
    VN_SET(p, VERSION);                     /* version 4 */ \
    MODE_SET(p, M_CLNT);                    /* mode client */ \
    STRATUM_SET(p, (char)MAXSTRAT);         /* unspecified */ \
    POLL_SET(p, (char)MINPOLL);             /* MINPOLL */ \
    PRECISION_SET(p, (char)CKPRECISION);    /* precision (2 complement): -18 -> 10^-4 (microseconds) */ \
    packet.refid = SwapInt32HostToBig('NTPS'); \
} while (0)

#define HLFP(a)         ((unsigned long long)(a) >> 32)
#define LLFP(a)         ((a) & 0xFFFFFFFF)
#define SWAP_LFP(a)     (HLFP(a) | (LLFP(a) << 32))
#define HB32T(p)        (*(int32_t *)(p) = SwapInt32HostToBig(*(int32_t *)(p)))
#define BH32T(p)        (*(int32_t *)(p) = SwapInt32BigToHost(*(int32_t *)(p)))
#define SWAPHB_LFP(l)   HB32T(l); HB32T((int32_t *)(l)+1)
#define SWAPBH_LFP(l)   BH32T(l); BH32T((int32_t *)(l)+1)

static void _ntp_host_2_big(tNtpPkt *p) {
    p->lvmspp = SwapInt32HostToBig(p->lvmspp);
    p->rootdelay = SwapInt32HostToBig(p->rootdelay);
    p->rootdisp = SwapInt32HostToBig(p->rootdisp);
    p->refid = SwapInt32HostToBig(p->refid);
    p->reference_ts = SwapInt64HostToBig(p->reference_ts);
    p->origin_ts = SwapInt64HostToBig(p->origin_ts);
    p->receive_ts = SwapInt64HostToBig(p->receive_ts);
    p->transmit_ts = SwapInt64HostToBig(p->transmit_ts);
}

static void _ntp_big_2_host(tNtpPkt *p) {
    p->lvmspp = SwapInt32BigToHost(p->lvmspp);
    p->rootdelay = SwapInt32BigToHost(p->rootdelay);
    p->rootdisp = SwapInt32BigToHost(p->rootdisp);
    p->refid = SwapInt32BigToHost(p->refid);
    SWAPBH_LFP(&p->reference_ts);
    SWAPBH_LFP(&p->origin_ts);
    SWAPBH_LFP(&p->receive_ts);
    SWAPBH_LFP(&p->transmit_ts);
#if LITTLE_ENDIAN
    p->reference_ts = SWAP_LFP(p->reference_ts);
    p->origin_ts = SWAP_LFP(p->origin_ts);
    p->receive_ts = SWAP_LFP(p->receive_ts);
    p->transmit_ts = SWAP_LFP(p->transmit_ts);
#endif
}

static char *_ntp_print(char *buf, int size, tNtpPkt *p) {

    snprintf(buf, size,  "NTP Packet:\n" \
                "   Leap year: %d\n" \
                "   Version: %d\n" \
                "   Mode: %d\n" \
                "   Stratum: %hhu\n" \
                "   Polling interval: %hhu\n" \
                "   Precision: %hhd\n" \
                "   Root delay: %f\n" \
                "   Root dispersion: %f\n" \
                "   Reference ID: %-.4s\n" \
                "   Reference ts: %f\n" \
                "   Origin ts: %f\n" \
                "   Receive ts: %f\n" \
                "   Transmit ts: %f\n",
                (int)LI(p),
                (int)VN(p),
                (int)MODE(p),
                (char)STRATUM(p),
                (char)POLL(p),
                (signed char)PRECISION(p),
                FP2D(p->rootdelay),
                FP2D(p->rootdisp),
                (char *)&p->refid,
                p->reference_ts ? LFP2D(LFP70(p->reference_ts)) : 0,
                p->origin_ts ? LFP2D(LFP70(p->origin_ts)) : 0,
                p->receive_ts ? LFP2D(LFP70(p->receive_ts)) : 0,
                p->transmit_ts ? LFP2D(LFP70(p->transmit_ts)) : 0);
    return buf;
}

#define TRIALS 20

static void _init_time(tTime *pT) {
    struct timeval unix_time[TRIALS];
    double delays[TRIALS];
    double tzero_sys[TRIALS];
    int i = 0, best = 0;

    for(i = 0; i < TRIALS; i++) {
        delays[i] = GETSECS();
        gettimeofday(&unix_time[i], NULL);
        tzero_sys[i] = GETSECS();
        delays[i] = tzero_sys[i] - delays[i];
        best = delays[i]  < delays[best] ? i : best;
    }

    memset(pT, 0, sizeof(tTime));
    pT->tzero_wck = (double)unix_time[best].tv_sec + (double)unix_time[best].tv_usec / 1e6;
    pT->tzero_ntp_wck = U2LFP(unix_time[best]);
    pT->tzero_sys = tzero_sys[best] - delays[best]/2;

    DEBUG_LEVEL(DEBUG_MEDIUM, NTPSYNC_DBG("-- Init (%d): tsys = %.10f delay = %.10f ts(ux/ntp) = %.10f/%lu.%lu\n", best, pT->tzero_sys, delays[best], pT->tzero_wck, (unsigned long)HLFP(pT->tzero_ntp_wck), (unsigned long)LLFP(pT->tzero_ntp_wck)));
}

#undef TRIALS

#define INTER_SYNC_DELAY_MIN    1000000         // usecs

#define _USLEEP_COND(max, step, exit_cond) do { \
    int _delay = 0, _step; \
    do { \
        _step = (max) - _delay > (step) ? (step) : (max) - _delay; \
        _delay += _step; \
        usleep(_step); \
    } while(!(exit_cond) && _delay < (max)); \
} while(0);


// Slew the clock at 1 correction each 1 ms to decrease the chance to invert the monotonicity of the psy timestamps
// With max_offset = 0.0005 sec, that means at most 0.5 us correction each ms
static void _slew_clock(tTime *pT, double max_offset) {
    double range_ms = (ABS(pT->ofs_rel) / max_offset) * 2 * 1000;
    double inc = pT->ofs_rel / range_ms;

    DEBUG_LEVEL(DEBUG_MEDIUM, NTPSYNC_DBG("-- Slewing clock by %f ms per ms, for the next %f ms\n", inc * 1000, range_ms));

    if (inc > 0) {
        while(pT->slewed_offset + inc < pT->offset) {
            pT->slewed_offset += inc;
            usleep(max_offset * 2 * 1000000);
        }
    } else {
        while(pT->slewed_offset + inc > pT->offset) {
            pT->slewed_offset += inc;
            usleep(max_offset * 2 * 1000000);
        }
    }
    pT->slewed_offset = pT->offset;
}

static void _adjust_clock(tTime *pTime, tTimeStats pStats[NTP_PKT_BUF_SZ], double max_offset) {
    double uncertainty[NTP_PKT_BUF_SZ];
    int i, best = 0, best_count = 0;

    for (i = 0; i < NTP_PKT_BUF_SZ; i++) {
        uncertainty[i] = (pStats[i].send_ts[1] - pStats[i].send_ts[0]) + (pStats[i].recv_ts[1] - pStats[i].recv_ts[0]); //- pStats[i].delay;
        best_count += uncertainty[i] == uncertainty[best] ? 1 : 0;
        best = uncertainty[i] < uncertainty[best] ? i : best;
        DEBUG_LEVEL(DEBUG_DEEP, NTPSYNC_DBG("-- %d, Uncertainty %f, delay = %f, offset = %f\n", i, uncertainty[i], pStats[i].delay, pStats[i].offset));
    }

    DEBUG_LEVEL(DEBUG_DEEP, NTPSYNC_DBG("-- Found %d possible optimal choices\n", best_count));

    if (best_count > 1) {
        for (i = 0; best_count > 0 && i < NTP_PKT_BUF_SZ; i++) {
            if (uncertainty[i] == uncertainty[best]) {
                best = pStats[i].delay < pStats[best].delay ? i : best;
                best_count--;
            }
        }
    }

    pTime->offset += pStats[best].offset;
    pTime->tsync_sys = GETSECS();
    pTime->delay = pStats[best].delay;
    pTime->ofs_rel = pStats[best].offset;

    pTime->adjustements++;
    // do this only after the first adjustement
    if (pTime->adjustements == 2)
        pTime->ofs_rel_max = pTime->ofs_rel_min = pStats[best].offset;
    else
    if (pTime->adjustements > 2) {
        pTime->ofs_rel_max = MAX(pTime->ofs_rel_max, pStats[best].offset);
        pTime->ofs_rel_min = MIN(pTime->ofs_rel_min, pStats[best].offset);
    }

    DEBUG_LEVEL(DEBUG_MEDIUM, NTPSYNC_DBG("-- CKADJ %d, best choice: abs-ofs = %f rel-ofs(min = %f / cur = %f / max = %f), delay = %f, sync = %f, measurement delay = %f\n", pTime->adjustements, pTime->offset, pTime->ofs_rel_min, pTime->ofs_rel, pTime->ofs_rel_max, pTime->delay, pTime->tsync_sys, uncertainty[best]));

    if (pTime->adjustements == 1) { // adjust clock abruptely
        pTime->slewed_offset = pTime->offset;
        DEBUG_LEVEL(DEBUG_MEDIUM, NTPSYNC_DBG("-- First synch, clock offset set to %f\n", pTime->offset));
    } else // do it slowly
        _slew_clock(pTime, max_offset);
}

static void _error(tNtpTime *pNtp, eNtpSyncError what) {
    pNtp->error = what;

    if (pNtp->cb_err != NULL)
        pNtp->cb_err(pNtp->error, pNtp->cb_err_prm);
}

static void *_ntp_sync(void *prm) {
    tNtpPkt pbuf[NTP_PKT_BUF_SZ], packet, packet_dbg;
    int inter_sync_delay = INTER_SYNC_DELAY_MIN;
    tNtpTime *pNtp = (tNtpTime *)prm;
    tTimeStats ts[NTP_PKT_BUF_SZ];
    tstamp org, rec, xmt;
    int i = 0, ignore;
    double max_offset;
    double delay;
    tstamp last_sync;

    DEBUG_LEVEL(DEBUG_MEDIUM, NTPSYNC_DBG("%s", "-- Started\n"));
    _init_time(&pNtp->time);
    last_sync = 0;
    memset(&packet, 0, NTP_PACKET_SIZE);
    org = 0;
    rec = 0;

    while(!pNtp->stop) {
        ignore = 0;
        delay = GETSECS();

        SET_NTP_PACKET(&packet);
        // we send a not sync packet, so rootdelay and rootdisp are not going to be considered by the server
        packet.rootdisp = 0;
        packet.rootdelay = 0;
        packet.reference_ts = last_sync;
        DEBUG_LEVEL(DEBUG_DEEP, packet_dbg = packet); // this will affect the delay... but it's done only in debug mode DEBUG_DEEP

        _ntp_host_2_big(&packet); // on partial data, to shorten the delay
        ts[i].send_ts[0] = GETSECS();
        packet.transmit_ts = SwapInt64HostToBig(LOC_2_NTP(&pNtp->time, ts[i].send_ts[0]));

        // this will affect the delay... but it's done only in debug mode DEBUG_DEEP
        DEBUG_OPEN(DEBUG_DEEP)
        char buf[512];
        packet_dbg.transmit_ts = SwapInt64BigToHost(packet.transmit_ts);
        NTPSYNC_DBG("-- (send) %s\n", _ntp_print(buf, sizeof(buf), &packet_dbg));
        DEBUG_CLOSE

        if (udp_send(pNtp->comm, (char *)&packet, NTP_PACKET_SIZE) != NTP_PACKET_SIZE) {
            _error(pNtp, eNtpSyncError_send);
            break;
        }

        ts[i].send_ts[1] = ts[i].recv_ts[0] = GETSECS();
        xmt = SwapInt64BigToHost(packet.transmit_ts);

        if (udp_receive(pNtp->comm, (char *)&packet, NTP_PACKET_SIZE) != NTP_PACKET_SIZE) {
            _error(pNtp, eNtpSyncError_receive);
            break;
        }

        ts[i].recv_ts[1] = GETSECS();

        _ntp_big_2_host(&packet);

        DEBUG_OPEN(DEBUG_DEEP)
        char buf[512];
        NTPSYNC_DBG("-- (recv) %s\n", _ntp_print(buf, sizeof(buf), &packet));
        DEBUG_CLOSE

        if (VN(&packet) > VERSION) {
            DEBUG_LEVEL(DEBUG_BASIC, NTPSYNC_DBG("-- Wrong version packet: (%d)\n", VN(&packet)));
            _error(pNtp, eNtpSyncError_version);
            break;
        }
        else
        if (STRATUM(&packet) == 0)  { // Kiss-Of-Death packet, ignore timestamps for they are unreliable and quit
            DEBUG_LEVEL(DEBUG_BASIC, NTPSYNC_DBG("-- Received a Kiss-Of-Death packet: (%-.4s)\n", (char *)&packet.refid));
            _error(pNtp, eNtpSyncError_kod);
            break;
        } else {
            pbuf[i] = packet;
            packet.receive_ts = LOC_2_NTP(&pNtp->time, ts[i].recv_ts[1]);

            if (MODE(&packet) == M_BCST) {
                DEBUG_LEVEL(DEBUG_BASIC, NTPSYNC_DBG("%s", "-- Broadcast packet: ignore\n"));
                ignore++;
            }
            else
            if (packet.transmit_ts == 0) { // invalid timestamp: something is badly wrong
                DEBUG_LEVEL(DEBUG_BASIC, NTPSYNC_DBG("%s", "-- Invalid timestamp\n"));
                _error(pNtp, eNtpSyncError_unexpected);
                break;
            }
            else
            if (xmt == packet.transmit_ts) { // check for duplicate or replay
                DEBUG_LEVEL(DEBUG_BASIC, NTPSYNC_DBG("%s", "-- Duplicate or replay: ignore\n"));
                ignore++;
            }
            else
            if (org == packet.transmit_ts) { // check for bogus
                DEBUG_LEVEL(DEBUG_BASIC, NTPSYNC_DBG("%s", "-- Bogus: ignore\n"));
                ignore++;
            }
            else
            if (LI(&packet) == NOSYNC || STRATUM(&packet) >= MAXSTRAT || STRATUM(&packet) == 0) {
                DEBUG_LEVEL(DEBUG_BASIC, NTPSYNC_DBG("%s", "-- Unsynchronised source\n"));
                ignore++;
            }
            else
            if (FP2D(packet.rootdelay) / 2 + FP2D(packet.rootdisp) >= MAXDISP || packet.reference_ts > packet.transmit_ts) {
                DEBUG_LEVEL(DEBUG_BASIC, NTPSYNC_DBG("%s", "-- Invalid header values\n"));
                ignore++;
            }
            rec = packet.receive_ts;
            org = packet.origin_ts = packet.transmit_ts;

            if (!ignore) {
                double t1, t2, t3, t4;

                t1 = LFP2D(LFP70(xmt));
                t2 = LFP2D(LFP70(pbuf[i].receive_ts));
                t3 = LFP2D(LFP70(pbuf[i].transmit_ts));
                t4 = LFP2D(LFP70(packet.receive_ts));

                ts[i].offset = (t2 - t1 + t3 - t4) / 2;
                ts[i].delay  = (t4 - t1) - (t3 - t2);
                ts[i].dispersion = LOG2D((signed char)PRECISION(&packet)) + LOG2D(CKPRECISION) + PHI*LFP2D(packet.receive_ts - pbuf[i].origin_ts);

                DEBUG_LEVEL(DEBUG_DEEP, NTPSYNC_DBG("-- Packet %d: relative offset = %f, delay = %f, dispersion = %f, (%f, %f, %f ,%f)\n", i, ts[i].offset, ts[i].delay, ts[i].dispersion, t1, t2, t3, t4));

                i = (i + 1) % NTP_PKT_BUF_SZ;

                if (i == 0) { // adjust the clock every NTP_PKT_BUF_SZ packets received
                    max_offset = pNtp->time.adjustements == 0 ? 0 : pNtp->max_offset;
                    _adjust_clock(&pNtp->time, ts, max_offset);
                    last_sync = LOC_2_NTP(&pNtp->time, pNtp->time.tsync_sys);

                    if (ABS(pNtp->time.ofs_rel) < pNtp->max_offset)
                        pNtp->synchronised = 1;
                    else
                    if (pNtp->time.adjustements > 2 || pNtp->synchronised) {
                        DEBUG_LEVEL(DEBUG_BASIC, NTPSYNC_DBG("-- Cannot synchronise: current relative offset = %f\n", pNtp->time.ofs_rel));
                        _error(pNtp, eNtpSyncError_accuracy_broken);
                        break;
                    }
                }
            }

            if (i == 0 && !ignore) {
                delay = (GETSECS() - delay) * 1000000; // usecs
                DEBUG_LEVEL(DEBUG_MEDIUM, NTPSYNC_DBG("-- Sleeping for %d us\n", inter_sync_delay - (int)delay));
                if (inter_sync_delay - delay >= 0)
                    _USLEEP_COND(inter_sync_delay - (int)delay, 1000000, pNtp->stop); // sleep but also check for exit condition
                inter_sync_delay = inter_sync_delay * 2 > pNtp->inter_sync_delay*1000 ? pNtp->inter_sync_delay*1000 : inter_sync_delay * 2;
            }

        }
    }
    DEBUG_LEVEL(DEBUG_MEDIUM, NTPSYNC_DBG("%s", "-- Quitted\n"));
    return NULL;
}

//---- The timer implementation

#define NTP_SRV_PORT 123

static pthread_t s_sync_thread;
static tNtpTime s_ntp_sync;

#define _Get_Millisec() (SLEWED_UNIX_TIME(&s_ntp_sync.time) * 1000)

void ntp_sync_stop() {

    if (s_ntp_sync.inited) {
        s_ntp_sync.stop = 1;
        pthread_join(s_sync_thread, NULL);
        udp_close(s_ntp_sync.comm);
    }

    if (s_ntp_sync.error)
        DEBUG_LEVEL(DEBUG_BASIC, NTPSYNC_DBG("%s","-- An NTP synchronisation occurred.\nTimestamps are not reliable\n"));

    s_ntp_sync.inited = 0;
}

void ntp_sync_set_time(double ms) {
    // Here: measure the time this operation costs...
    s_ntp_sync.ntp_start_time = (tstamp)((uint64_t)SLEWED_NTP_TIME(&s_ntp_sync.time) - D2LFP(ms/1000));
	s_ntp_sync.start_time = LFP2D(LFP70(s_ntp_sync.ntp_start_time)) * 1000;
}

int ntp_sync_start(char *ip_address, double max_offset_ms, int inter_sync_delay_ms) {
    int rc = 1;

    if (inter_sync_delay_ms * 1000 <= INTER_SYNC_DELAY_MIN) {
        DEBUG_LEVEL(DEBUG_BASIC, NTPSYNC_DBG("-- Inter synchronisation delay must be > %d ms\n", INTER_SYNC_DELAY_MIN/1000));
        goto quit;
    }

    memset(&s_ntp_sync, 0 , sizeof(s_ntp_sync));
    s_ntp_sync.comm = udp_open(ip_address, NTP_SRV_PORT, 500000);

    if (s_ntp_sync.comm <= 0) {
        DEBUG_LEVEL(DEBUG_BASIC, NTPSYNC_DBG("-- Failed to init the UDP connection with %s:%d\n", ip_address, NTP_SRV_PORT));
        goto quit;
    }

    s_ntp_sync.inited = 1;
    s_ntp_sync.max_offset = max_offset_ms / 1000;
    s_ntp_sync.inter_sync_delay = inter_sync_delay_ms;

    if (pthread_create(&s_sync_thread, NULL, _ntp_sync, &s_ntp_sync) != 0) {
        udp_close(s_ntp_sync.comm);
        DEBUG_LEVEL(DEBUG_BASIC, NTPSYNC_DBG("%s", "-- Failed to start the timer\n"));
        goto quit;
    }

    while(!s_ntp_sync.synchronised && !s_ntp_sync.error)
        usleep(500000);

    if (s_ntp_sync.error) {
        DEBUG_LEVEL(DEBUG_BASIC, NTPSYNC_DBG("%s", "-- NTP synchronisation error\n"));
    } else {
        ntp_sync_set_time(0);
        rc = 0;
    }

quit:
    return rc;
}

double ntp_sync_get_time() {
    return _Get_Millisec() - s_ntp_sync.start_time;
}

double ntp_sync_start_time() {
    return s_ntp_sync.start_time;
}

int ntp_sync_error() {
    return s_ntp_sync.error;
}

void ntp_sync_on_error(tCbOnErr cb, void *prm) {
    s_ntp_sync.cb_err = cb;
    s_ntp_sync.cb_err_prm = prm;
}

double ntp_sync_monotonic_time() {
    return GETSECS() * 1000;
}
