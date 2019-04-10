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

#ifndef _NOMPI
#include <mpi.h>
#endif

#include "PoLiLog.h"
//#include "PoLiMEr.h"
#include "power_cap_handler.h"
#include "helpers.h"

#ifdef _MSR
#include "msr_handler.h"
#endif

/* THIS IS ONLY FOR KNL! */
//we need to know what the power capping zones are
char *zone_names[2] = {"PACKAGE", "DRAM"}; //"CORE", "DRAM"};
//for easier string manipulation
int zone_names_len[2] = {7,4}; //,4};

static int init_pcap_tag (char *zone, double watts_long, double watts_short, double seconds_long, 
    double seconds_short, pcap_flag_t pcap_flag, struct system_info_t * system_info,
    struct monitor_t * monitor, struct poller_t * poller);
static int get_system_power_cap_for_zone (int zone_index, struct system_info_t * system_info, struct monitor_t * monitor);

int get_zone_index (char *zone_name, char * zone_names[], int zone_names_len[], struct system_info_t * system_info)
{
#ifdef _MSR
    int i;
    for (i = 0; i < system_info->sysmsr->num_zones; i++)
    {
        if (strncmp(zone_name, zone_names[i], sizeof(zone_names[i])) == 0)
            return i;
    }
#endif
    return -1;
}

static int init_pcap_tag (char *zone, double watts_long, double watts_short, double seconds_long, 
    double seconds_short, pcap_flag_t pcap_flag, struct system_info_t * system_info,
    struct monitor_t * monitor, struct poller_t * poller)
{
    if (monitor->imonitor)
    {
        poli_log(TRACE, monitor, "Entering %s", __FUNCTION__);

        /* check if requested zone is an allowed parameter */
        int i = get_zone_index(zone, zone_names, zone_names_len, system_info);
        if ( i < 0)
        {
            poli_log(ERROR, monitor, "Invalid zone name: %s !", zone);
            return 1;
        }

        struct pcap_tag *new_pcap_tag = &system_info->pcap_tag_list[system_info->num_pcap_tags];

        new_pcap_tag->id = system_info->num_pcap_tags;
        new_pcap_tag->monitor_id = monitor->color;
        new_pcap_tag->monitor_rank = monitor->world_rank;

        memset(new_pcap_tag->zone, '\0', ZONE_NAME_LEN);
        strncpy(new_pcap_tag->zone, zone_names[i], zone_names_len[i]);

        new_pcap_tag->watts_long = watts_long;
        new_pcap_tag->watts_short = watts_short;
        new_pcap_tag->seconds_long = seconds_long;
        new_pcap_tag->seconds_short = seconds_short;

        new_pcap_tag->wtime = get_time();
        gettimeofday(&(new_pcap_tag->timestamp), NULL);

        new_pcap_tag->start_timer_count = poller->time_counter;
        new_pcap_tag->pcap_flag = pcap_flag;

        int found_num = 0;
        int tag_num;

        for (tag_num = 0; tag_num < system_info->num_poli_tags; tag_num++)
        {
            struct poli_tag current_poli_tag = system_info->poli_tag_list[tag_num];
            if (current_poli_tag.closed == 0)
            {
                new_pcap_tag->active_poli_tags[found_num] = current_poli_tag;
                found_num++;
            }
        }

        new_pcap_tag->num_active_poli_tags = found_num;

        system_info->num_pcap_tags++;

        poli_log(TRACE, monitor, "Finishing %s", __FUNCTION__);

    }

    return 0;
}

char * get_zone_name_by_index(int index)
{
    return zone_names[index];
}


//todo make this more template like
int set_power_cap (double watts, struct system_info_t * system_info,
    struct monitor_t * monitor, struct poller_t * poller, struct polimer_config_t * poli_config)
{
#ifdef _MSR
    if (poli_config->cap_short_window)
        return set_power_cap_with_params(zone_names[PACKAGE_INDEX], watts, watts,
        DEFAULT_SECONDS_LONG, DEFAULT_SECONDS_SHORT, system_info, monitor, poller);
    else
        return set_power_cap_with_params(zone_names[PACKAGE_INDEX], watts, DEFAULT_SHORT,
            DEFAULT_SECONDS_LONG, DEFAULT_SECONDS_SHORT, system_info, monitor, poller);
#else
    return 0;
#endif
}

int set_power_cap_with_params(char *zone_name, double watts_long, double watts_short,
    double seconds_long, double seconds_short, struct system_info_t * system_info,
    struct monitor_t * monitor, struct poller_t * poller)
{
#ifdef _MSR
    if (monitor->imonitor)
    {
        poli_log(TRACE, monitor, "Entering %s", __FUNCTION__);

        int i = get_zone_index(zone_name, zone_names, zone_names_len, system_info);
        if (i < 0)
        {
            poli_log(ERROR, monitor, "Something went wrong with getting index for zone %s. Are you sure the zone name is valid?", zone_name);
            return 1;
        }

        if (rapl_set_power_cap(zone_name, watts_long, watts_short, seconds_long, seconds_short, system_info, 1) != 0)
        {
            poli_log(ERROR, monitor,   "%s: Something went wrong with setting rapl power cap!", __FUNCTION__);
            return 1;
        }

        if (init_pcap_tag(zone_name, watts_long, watts_short, seconds_long, seconds_short,
            USER_SET, system_info, monitor, poller) != 0)
        {
            poli_log(ERROR, monitor,   "%s: Something went wrong with initializing a new power cap tag!", __FUNCTION__);
            return 1;
        }

        struct pcap_info *info = &system_info->current_pcap_list[i];

        info->monitor_id = monitor->color;
        info->monitor_rank = monitor->world_rank;
        memset(info->zone, '\0', ZONE_NAME_LEN);
        strncpy(info->zone, zone_name, zone_names_len[i]);
        if (watts_long > 0)
        {
            info->enabled_long = 1;
            info->clamped_long = 1;
        }
        else
        {
            info->enabled_long = 0;
            info->clamped_long = 0;
        }
        if (watts_short > 0)
        {
            info->enabled_short = 1;
            info->clamped_short = 1;
        }
        else
        {
            info->enabled_short = 0;
            info->clamped_short = 0;
        }
        info->watts_long = watts_long;
        info->watts_short = watts_short;
        info->seconds_long = seconds_long;
        info->seconds_short = seconds_short;

        poli_log(TRACE, monitor, "Finishing %s", __FUNCTION__);
    }
#endif
    return 0;
}

int reset_system (struct system_info_t * system_info, struct monitor_t * monitor, struct poller_t * poller)
{
#ifdef _MSR
    if (monitor->imonitor)
    {
        poli_log(TRACE, monitor, "Entering %s", __FUNCTION__);

        //if (rapl_set_power_cap("PACKAGE", (double) DEFAULT_PKG_POW, (double) DEFAULT_SHORT, (double) DEFAULT_SECONDS_LONG, (double) DEFAULT_SECONDS_SHORT, system_info, 1) ||
        //    rapl_set_power_cap("CORE", (double) DEFAULT_CORE_POW, 0, (double) DEFAULT_CORE_SECONDS, 0, system_info, 0))
        if (rapl_set_power_cap("PACKAGE", (double) DEFAULT_PKG_POW, (double) DEFAULT_SHORT, (double) DEFAULT_SECONDS_LONG, (double) DEFAULT_SECONDS_SHORT, system_info, 1))
        {
            poli_log(ERROR, monitor,   "%s: Something went wrong with setting power caps. Returning...\n", __FUNCTION__);
            return 1;
        }

        /* Set up new pcap tags to indicate change in power caps */
        //if (init_pcap_tag("PACKAGE", (double) DEFAULT_PKG_POW, (double) DEFAULT_SHORT, (double) DEFAULT_SECONDS_LONG, (double) DEFAULT_SECONDS_SHORT, SYSTEM_RESET) != 0 ||
        //   init_pcap_tag("CORE", (double) DEFAULT_CORE_POW, 0, (double) DEFAULT_SECONDS_LONG, 0, SYSTEM_RESET) != 0)
        if (init_pcap_tag("PACKAGE", (double) DEFAULT_PKG_POW, (double) DEFAULT_SHORT, 
            (double) DEFAULT_SECONDS_LONG, (double) DEFAULT_SECONDS_SHORT, SYSTEM_RESET,
            system_info, monitor, poller) != 0)
        {
            poli_log(ERROR, monitor,   "%s: Something went wrong with initializing a new power cap tag!\n", __FUNCTION__);
            return 1;
        }

        get_system_power_caps(system_info, monitor); //to reset the system_info->current_pcap_list

        poli_log(TRACE, monitor, "Finishing %s", __FUNCTION__);
    }
#endif
    return 0;
}

int get_power_cap_package (char *param, double *result,
    struct system_info_t * system_info, struct monitor_t * monitor)
{
    return get_power_cap_for_param(zone_names[PACKAGE_INDEX], 
        "watts_long", result, system_info, monitor);
}

int get_power_cap_for_param (char *zone_name, char *param, double *result,
    struct system_info_t * system_info, struct monitor_t * monitor)
{
#ifdef _MSR
    if (monitor->imonitor)
    {
        poli_log(TRACE, monitor,   "Entering %s", __FUNCTION__);

        int found = 0;
        int pkg_exists = 0;
        /* check if the package power cap is there, in case zone_name is invalid*/
        struct pcap_info *pkg_info = &system_info->current_pcap_list[PACKAGE_INDEX];
        if (pkg_info != 0)
            pkg_exists = 1;
        /* look for power cap of zone requested */
        int i = get_zone_index(zone_name, zone_names, zone_names_len, system_info);
        if (i > -1)
            found = 1;

        if ( !found && pkg_exists)
        {
            i = PACKAGE_INDEX;
            poli_log(WARNING, monitor, "Power cap for requested zone %s could not be found. Defaulting to zone PACKAGE", zone_name);
        }
        else if ( !found && !pkg_exists)
        {
            poli_log(ERROR, monitor, "%s: Could not find power cap parameter info for zone %s", zone_name, __FUNCTION__);
            *result = 0.0;
        }
        else
        {
            if (result != NULL)
            {
                struct pcap_info *info = &system_info->current_pcap_list[i];
                if (strcmp(param, "watts_long") == 0)
                    *result = info->watts_long;
                else if (strcmp(param, "watts_short") == 0)
                    *result = info->watts_short;
                else if (strcmp(param, "seconds_long") == 0)
                    *result = info->seconds_long;
                else if (strcmp(param, "seconds_short") == 0)
                    *result = info->seconds_short;
                else if (strcmp(param, "enabled_long") == 0)
                    *result = (double) info->enabled_long;
                else if (strcmp(param, "enabled_short") == 0)
                    *result = (double) info->enabled_short;
                else if (strcmp(param, "clamped_long") == 0)
                    *result = (double) info->clamped_long;
                else if (strcmp(param, "clamped_short") == 0)
                    *result = (double) info->clamped_short;
                else
                {
                    poli_log(WARNING, monitor, "The parameter: %s was not recognized. Returning watts_long for zone %s as default.", param, zone_name);
                    *result = info->watts_long;
                }
            }
        }

        poli_log(TRACE, monitor, "Finishing %s", __FUNCTION__);
    }

#ifndef _NOMPI
    MPI_Bcast(result, 1, MPI_DOUBLE, 0, monitor->mynode_comm); //this is necessary for user to retrieve value
#endif
#endif
    return 0;
}

int get_power_cap (double *watts, struct system_info_t * system_info, struct monitor_t * monitor)
{
#ifdef _MSR
    return get_power_cap_for_param(zone_names[PACKAGE_INDEX], "watts_long", watts, system_info, monitor);
#else
    return 0;
#endif
}

static int get_system_power_cap_for_zone (int zone_index, struct system_info_t * system_info, struct monitor_t * monitor)
{
#ifdef _MSR
    if (monitor->imonitor)
    {
        struct msr_pcap pcap;
        int pret = rapl_get_power_cap(&pcap, zone_names[zone_index], system_info);
        if (pret != 0)
        {
            poli_log(ERROR, monitor, "%s: Something went wrong with getting RAPL power cap", __FUNCTION__);
            return -1;
        }

        struct pcap_info *info = &system_info->current_pcap_list[zone_index];

        info->monitor_id = monitor->color;
        info->monitor_rank = monitor->world_rank;
        memset(info->zone, '\0', ZONE_NAME_LEN);
        strncpy(info->zone, zone_names[zone_index], zone_names_len[zone_index]);
        info->zone_label = pcap.zone_label;
        info->enabled_long = pcap.enabled_long;
        info->watts_long = pcap.watts_long;
        info->watts_short = pcap.watts_short;
        info->seconds_long = pcap.seconds_long;
        info->seconds_short = pcap.seconds_short;
        info->enabled_short = pcap.enabled_short;
        info->clamped_long = pcap.clamped_long;
        info->clamped_short = pcap.clamped_short;
    }
#endif
    return 0;
}

int get_system_power_caps (struct system_info_t * system_info, struct monitor_t * monitor)
{
#ifdef _MSR
    if (monitor->imonitor)
    {
        poli_log(TRACE, monitor,   "Entering %s", __FUNCTION__);

        int i;
        for (i = 0; i < system_info->sysmsr->num_zones; i++)
            get_system_power_cap_for_zone(i, system_info, monitor);

        poli_log(TRACE, monitor,   "Finishing %s", __FUNCTION__);
    }
#endif
    return 0;
}

int get_power_cap_limits (char* zone_name, double *min, double *max,
    struct system_info_t * system_info, struct monitor_t * monitor)
{
#ifdef _MSR
    if (monitor->imonitor)
    {
        int index = get_zone_index(zone_name, zone_names, zone_names_len, system_info);
        struct pcap_info *current_pcap = &system_info->current_pcap_list[index];

        if (!current_pcap->min || !current_pcap->max)
            rapl_get_power_cap_info(zone_name, &current_pcap->min, &current_pcap->max,
                &current_pcap->thermal_spec, &current_pcap->max_time_window, system_info);

        *min = current_pcap->min;
        *max = current_pcap->max;
    }

#ifndef _NOMPI
    MPI_Bcast(min, 1, MPI_DOUBLE, 0, monitor->mynode_comm);
    MPI_Bcast(max, 1, MPI_DOUBLE, 0, monitor->mynode_comm);
#endif
#endif
    return 0;
}

void print_power_cap_info (struct system_info_t * system_info, struct monitor_t * monitor)
{
    if (monitor->imonitor)
    {
#ifdef _MSR
        printf("************************************************************\n");
        printf("                       POWER CAP INFO                       \n");
        printf("                       RANK: %d NODE: %s                    \n", monitor->world_rank, monitor->my_host);
        printf("------------------------------------------------------------\n");

        struct pcap_info *current_pcap = &system_info->current_pcap_list[PACKAGE_INDEX];
        if (current_pcap != 0)
        {
            printf("\tzone: %s\n", current_pcap->zone);
            printf("\twatts_long: %lf\n", current_pcap->watts_long);
            printf("\tseconds_long: %lf\n", current_pcap->seconds_long);
            printf("\tenabled_long: %d\n", current_pcap->enabled_long);
            printf("\tclamped_long: %d\n", current_pcap->clamped_long);
            printf("\twatts_short: %lf\n", current_pcap->watts_short);
            printf("\tseconds_short: %lf\n", current_pcap->seconds_short);
            printf("\tenabled_short: %d\n", current_pcap->enabled_short);
            printf("\tclamped_short: %d\n", current_pcap->clamped_short);
            printf("\tthermal specification: %lf\n", current_pcap->thermal_spec);
            printf("\tmax power cap %lf\n", current_pcap->max);
            printf("\tmin power cap %lf\n", current_pcap->min);
            printf("\tmax time window %lf\n", current_pcap->max_time_window);
        }
        else
            printf("Power cap for zone PACKAGE on rank %d has not yet been recorded.\n", monitor->world_rank);

        printf("************************************************************\n");
#else
        printf(" MSR flag is not set, cannot get power cap information.\n");
#endif
    }
}

void print_power_cap_info_verbose (struct system_info_t * system_info, struct monitor_t * monitor)
{
    if (monitor->imonitor)
    {
#ifdef _MSR
        int i;
        printf("************************************************************\n");
        printf("                POWER CAP INFO VERBOSE                      \n");
        printf("                RANK: %d NODE: %s                           \n", monitor->world_rank, monitor->my_host);
        printf("------------------------------------------------------------\n");
        for (i = 0; i < system_info->sysmsr->num_zones; i++)
        {
            struct pcap_info *current_pcap = &system_info->current_pcap_list[i];
            if (current_pcap != 0)
            {
                printf("\tzone: %s\n", current_pcap->zone);
                printf("\twatts_long: %lf\n", current_pcap->watts_long);
                printf("\tseconds_long: %lf\n", current_pcap->seconds_long);
                printf("\tenabled_long: %d\n", current_pcap->enabled_long);
                printf("\tclamped_long: %d\n", current_pcap->clamped_long);
                int short_supported = 0;
                if (current_pcap->zone_label == PACKAGE || current_pcap->zone_label == PLATFORM)
                    short_supported = 1;
                if (short_supported)
                {
                    printf("\twatts_short: %lf\n", current_pcap->watts_short);
                    printf("\tseconds_short: %lf\n", current_pcap->seconds_short);
                    printf("\tenabled_short: %d\n", current_pcap->enabled_short);
                    printf("\tclamped_short: %d\n", current_pcap->clamped_short);
                }
                printf("\tthermal specification: %lf\n", current_pcap->thermal_spec);
                printf("\tmax power cap %lf\n", current_pcap->max);
                printf("\tmin power cap %lf\n", current_pcap->min);
                printf("\tmax time window %lf\n", current_pcap->max_time_window);
            }
            else
                printf("Power cap for zone %s on rank %d has not yet been recorded.\n", zone_names[i], monitor->world_rank);
        }
        printf("************************************************************\n");
#else
        printf(" MSR flag is not set, cannot get power cap information.\n");
#endif
    }
}