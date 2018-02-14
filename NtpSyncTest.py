#
#  NtpSyncTest.py
#
#  Author: Filippin luca
#  luca.filippin@gmail.com
#

from argparse import ArgumentParser
import NtpSyncPy as nsp
import numpy as np
import sys
import time

def parse_options():
    parser = ArgumentParser(description='Ntp Sync Test')
    parser.add_argument('--version', action = 'version', version='%(prog)s v1.0')
    parser.add_argument("-a", "--server",  dest="server", default="127.0.0.1", help="Ntp ip server address")
    parser.add_argument("-o", "--offset", dest="offset", type=float, default=0.5, help="Max clock offset tolerated [ms]")
    parser.add_argument("-s", "--synch-period", dest="synch_period", type=int, default=5000, help="Inter synch delay [ms]")
    parser.add_argument("-p", "--stats-period", dest="stats_period", type=int, default=5000, help="Statistics period [ms]")

    try:
        o = parser.parse_args()
        if o.offset <= 0:
            raise Exception('Max clock offset must be a positive float')
        if o.synch_period <= 0:
            raise Exception('Max clock offset must be a positive int')
        if o.stats_period <= 0:
            raise Exception('Statistics period must be a positive int')
    except Exception, e:
        raise Exception("*** Argument error ***\n" + str(e))
    return o


if __name__ == '__main__':
    try:
        o = parse_options()
        nsp.ntp_sync_start(o.server, o.offset, o.synch_period)

        while True:
            before = nsp.ntp_sync_monotonic_time()
            curr_ts = nsp.ntp_sync_set_time(0)
            after = nsp.ntp_sync_monotonic_time()
            if after - before > 0.5:
                print 'Skip: setting timestamp required %.6f ms' %(after - before)
            else:
                break

        start_ts = nsp.ntp_sync_start_time()
        prev_ts = first_ts = -1
        delay = []
        measure = []
        skipped = 0

        while True:
            if nsp.ntp_sync_error():
                raise Exception('Synchronisation error (%d)' %nsp.ntp_sync_error())

            before = nsp.ntp_sync_monotonic_time()
            curr_ts = nsp.ntp_sync_get_time()
            after = nsp.ntp_sync_monotonic_time()
            gtofd = time.time()

            if after - before > 0.5:
                #print 'Skip: taking timestamp required %.6f ms' %(after - before)
                skipped += 1
                prev_ts = -1
                continue
            else:
                measure.append(after - before)

            if first_ts > 0:
                if prev_ts > 0:
                    delay.append(curr_ts - prev_ts)
                    if (curr_ts - first_ts >= o.stats_period):
                        unix_ts = (start_ts + curr_ts)/1000
                        print "%.6f [s] (%.3f) Delay (%.3f)[ms] min:%.6f, avg:%.6f, max:%.6f, std:%.6f, measure:%.6f, good:%d, skipped:%d, total:%d" %(unix_ts, unix_ts - gtofd, curr_ts - first_ts,  np.min(delay), np.mean(delay), np.max(delay), np.std(delay), np.mean(measure), len(delay), skipped, len(delay) + skipped)
                        first_ts = -1
                        skipped = 0
                        delay = []
                        measure = []
            else:
                first_ts = curr_ts

            prev_ts = curr_ts

    except KeyboardInterrupt:
        pass
    except Exception, e:
        print 'An unexpected error occurred: %s' %str(e)
    finally:
        nsp.ntp_sync_stop()

    print 'Done.'
