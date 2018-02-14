#ifndef __NTPSYNC_H__
#define __NTPSYNC_H__

typedef enum {
    eNtpSyncError_no,       // don't move this
    eNtpSyncError_send,
    eNtpSyncError_receive,
    eNtpSyncError_version,
    eNtpSyncError_kod,      // kiss of death
    eNtpSyncError_unexpected,
    eNtpSyncError_accuracy_broken
} eNtpSyncError;

typedef void (*tCbOnErr)(eNtpSyncError err, void *prm);

int ntp_sync_start(char *ip_address, double max_offset_ms, int inter_sync_delay_ms);
void ntp_sync_stop();
void ntp_sync_set_time(double ms);
double ntp_sync_get_time();
double ntp_sync_start_time();
int ntp_sync_error();
void ntp_sync_on_error(tCbOnErr cb, void *prm);
double ntp_sync_monotonic_time();

#endif