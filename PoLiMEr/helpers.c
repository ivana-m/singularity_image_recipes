#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <math.h>
#include <assert.h>
#include <stdarg.h>

#ifdef _NOMPI
#include <mpi.h>
#endif

#include "helpers.h"
#include "PoLiLog.h"
//#include "PoLiMEr.h"

#ifdef _MSR
#include "msr_handler.h"
#endif

#ifdef _BGQ
#include "bgq_handler.h"
#endif

#ifdef _CRAY
#include "cray_handler.h"
#endif

double get_time (void)
{
#ifndef _NOMPI
    return MPI_Wtime();
#else
    return (double) time(NULL);
#endif
}

void get_initial_time(struct system_info_t * system_info, struct monitor_t * monitor)
{
    int collective = 1;
    if (monitor->num_monitors == 1 || !collective)
        system_info->initial_mpi_wtime = get_time();
    else if (collective)
    {
        double stime = get_time();
        MPI_Allreduce(&stime, &system_info->initial_mpi_wtime, 1, MPI_DOUBLE, MPI_MIN, monitor->monitors_comm);
    }
    gettimeofday(&system_info->initial_start_time, NULL);
    return;
}

int compute_current_power (struct system_poll_info * info, double time, struct system_info_t * system_info)
{
#ifdef _MSR
    struct rapl_energy diff;
    rapl_compute_total_energy(&(diff), &(info->current_energy.rapl_energy), &(info->last_energy.rapl_energy));
    rapl_compute_total_power(&(info->computed_power.rapl_power), &(diff), time);
#elif _CRAY
    compute_cray_total_measurements(&(info->computed_power.cray_meas), &(info->current_energy.cray_meas), &(info->last_energy.cray_meas), time);
#elif _BGQ
    compute_bgq_total_measurements(&(info->computed_power.bgq_meas), &(info->current_energy.bgq_meas), &(info->last_energy.bgq_meas), time);
#else
    return 1;
#endif
    return 0;
}

struct energy_reading read_current_energy (struct system_info_t * system_info)
{
    struct energy_reading current_energy;
#ifdef _MSR
    rapl_read_energy(&(current_energy.rapl_energy), system_info);
#elif _CRAY
    get_cray_measurement(&(current_energy.cray_meas), system_info);
#elif _BGQ
    init_bgq_measurement(&(current_energy.bgq_meas));
    get_bgq_measurement(&(current_energy.bgq_meas), system_info);
#endif
    return current_energy;
}

void get_timestamp(double time_from_start, char *time_str_buffer, size_t buff_len,
    struct timeval * initial_start_time)
{
    double frac, intpart;
    int64_t fracpart;
    struct timeval cur_time;

    char time_str[8192];
    memset(time_str, '\0', 8192);

    frac = modf(time_from_start, &intpart);
    cur_time.tv_sec = initial_start_time->tv_sec + (int64_t) intpart;
    fracpart = (int32_t) (frac * 1000000.0);

    if ((initial_start_time->tv_usec + fracpart) >= 1e6) {
        cur_time.tv_usec = (initial_start_time->tv_usec + fracpart - 1e6);
        cur_time.tv_sec++;
    }
    else {
        cur_time.tv_usec = initial_start_time->tv_usec + fracpart;
    }

    time_t rawtime = (time_t)cur_time.tv_sec;
    struct tm *timeinfo = localtime(&rawtime);

    strftime (time_str_buffer, buff_len, "%Y-%m-%d %H:%M:%S", timeinfo);

    return;

}


FILE * open_file (char *filename, struct monitor_t * monitor)
{
    FILE * fp;
    char *prefix;
    prefix = getenv("PoLi_PREFIX");
    char file[1000];
    if (prefix != NULL)
    {
        if (strcmp(filename, "simulation-end-pcap.txt") == 0 || strcmp(filename, "analysis-end-pcap.txt") == 0)
            sprintf(file, "%s%s", prefix, filename);
        else
            sprintf(file, "%s%s_%s_%s.txt", prefix, filename, monitor->my_host, monitor->jobid);
    }
    else
        sprintf(file, "%s_%s_%s.txt", filename, monitor->my_host, monitor->jobid);

    fp = fopen(file, "w");
    if (!fp)
    {
        poli_log(ERROR, monitor,   "Failed to open file %s: %s", file, strerror(errno));
        fclose(fp);
        return NULL;
    }
    return fp;
}

/*
coordsToInt - composes an integer out of the coordinates on an Aries router
input: the coordinates to convert and the number of coordinates
returns: the resulting integer
*/
// (x, y, z) == (2, 3, 4) becomes res = 234
int coordsToInt (int *coords, int dim)
{
    int i, res = 0;

    for ( i = 0; i < dim; i++ )
        res += coords[i] * pow (10.0, (double)i);

    return res;
}

int compute_power_from_tag(struct poli_tag *tag, double time)
{
#ifdef _MSR
    rapl_compute_total_energy(&(tag->total_energy.rapl_energy), &(tag->end_energy.rapl_energy), &(tag->start_energy.rapl_energy));
    rapl_compute_total_power(&(tag->total_power.rapl_power), &(tag->total_energy.rapl_energy), time);
#endif
#ifdef _CRAY
    compute_cray_total_measurements(&(tag->total_energy.cray_meas), &(tag->end_energy.cray_meas), &(tag->start_energy.cray_meas), time);
#elif _BGQ
    compute_bgq_total_measurements(&(tag->total_energy.bgq_meas), &(tag->end_energy.bgq_meas), &(tag->start_energy.bgq_meas), time);
#endif

    return 0;
}