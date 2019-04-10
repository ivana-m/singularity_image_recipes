#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <math.h>

#include "PoLiLog.h"
//#include "PoLiMEr.h"

#include "frequency_handler.h"

static int read_cpufreq (struct system_info_t * system_info, struct monitor_t * monitor, double *freq, struct poller_t * poller);


int get_current_frequency_value (struct system_info_t * system_info, struct monitor_t * monitor, double *freq, struct poller_t * poller)
{
    int ret = 0;
    if (monitor->imonitor)
    {
        struct system_poll_info info;
        get_current_frequency_info(system_info, monitor, &info, poller);
        double cpufreq = info.freq.freq;
        if (cpufreq == 0.0)
        {
            poli_log(ERROR, monitor,   "Unable to get frequency from cpufreq");
            ret = 1;
        }
        else
        {
            poli_log(TRACE, monitor,   "Frequency obtained from cpufreq: %lf KHz\n", cpufreq);
            (*freq) = cpufreq;
            return 0;
        }

#ifdef _CRAY
        double crayfreq = info.freq.cray_freq;
        if (crayfreq == 0.0)
        {
            poli_log(ERROR, monitor,   "Unable to get frequency from Cray stack.");
            ret = 1;
        }
        else
        {
            poli_log(TRACE, monitor,   "Frequency obtained from Cray stack: %lf KHz\n", crayfreq);
            (*freq) = crayfreq;
            return 0;
        }
#endif
    }
    return ret;
}

int print_frequency_info (struct system_info_t * system_info, struct monitor_t * monitor, struct poller_t * poller)
{
    struct system_poll_info info;
    get_current_frequency_info(system_info, monitor, &info, poller);
    double cpufreq = info.freq.freq;
    printf("************************************************************\n");
    printf("                     FREQUENCY INFO                         \n");
    printf("                     RANK: %d NODE: %s                      \n", monitor->world_rank, monitor->my_host);
    printf("------------------------------------------------------------\n");
    printf("Frequency obtained from cpufreq: %lf KHz\n", cpufreq);
#ifdef _CRAY
    double crayfreq = info.freq.cray_freq;
    printf("Frequency obtained from Cray stack: %lf KHz\n", crayfreq);
#endif
    printf("************************************************************\n");
    return 0;
}

int get_current_frequency_info (struct system_info_t * system_info, struct monitor_t * monitor, struct system_poll_info * info, struct poller_t * poller)
{
    int fret = read_cpufreq(system_info, monitor, &(info->freq.freq), poller);
    if (fret)
    {
        poli_log(ERROR, monitor,   "%s: Something went wrong with getting frequency form cpufreq", __FUNCTION__);
        info->freq.freq = 0.0;
    }
#ifdef _CRAY
    info->freq.cray_freq = cray_read_pm_counter(system_info->syscray->counters[CRAY_FREQ_INDEX].pm_file);
#endif
    return 0;
}

static int read_cpufreq (struct system_info_t * system_info, struct monitor_t * monitor, double *freq, struct poller_t * poller)
{
    if (monitor->imonitor)
    {
        char buff[200];
        memset(buff, '\0', sizeof(buff));

        int hz = 0;
        if (system_info->cur_freq_file > 0)
        {
            if (poller->time_counter > 0)
            {
                if(lseek(system_info->cur_freq_file, 0, SEEK_SET) < 0)
                {
                    poli_log(ERROR, monitor,   "Attempt to set file descriptor to beginning of frequency file failed! %s", strerror(errno));
                    return 1;
                }
            }
            if (read(system_info->cur_freq_file, buff, sizeof(buff)))
            {
                char *token;
                token = strtok(buff, " \t\n");
                hz = atoi(token);
            }

            (*freq) = (double) (hz / 1000.0);
        }
        else
            (*freq) = 0.0;
    }
    return 0;
}


