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

#include "helpers.h"
#include "frequency_handler.h"
#include "power_cap_handler.h"
#include "output.h"

#ifndef _NOMPI
#include <mpi.h>
#include "mpi_handler.h"
#endif
#ifndef _NOOMP
#include <omp.h>
#endif

#ifdef _PMI
#include <pmi.h>
#endif

#include "PoLiMEr.h"
#include "PoLiLog.h"

#ifdef _MSR
#include "msr_handler.h"
#endif

#ifdef _CRAY
#include "cray_handler.h"
#endif

#ifdef _BGQ
#include "bgq_handler.h"
#endif

#ifdef _POWMGR
#include "power_manager.h"
#endif

struct monitor_t *monitor = 0;
struct polimer_config_t *poli_config = 0;
struct poller_t *poller = 0;

//the main struct holding the entire system states used througout the program
struct system_info_t *system_info = 0;

static void init_system_info (void);
static void get_jobid (void);

static void init_power_interfaces (struct system_info_t * system_info);
static void finalize_power_interfaces (struct system_info_t * system_info);

static struct poli_tag *get_poli_tag_for_end_time_counter(int counter);
static int start_poli_tag_no_sync (char *tag_name);
static int end_poli_tag_no_sync (char *tag_name);
static struct poli_tag *find_poli_tag_for_name (char *tag_name);
static struct poli_tag *get_poli_tag_for_start_time_counter(int counter);
static int end_existing_poli_tag (struct poli_tag *this_poli_tag);

#ifndef _TIMER_OFF
static int setup_timer (void);
static int stop_timer (void);
static void timer_handler (int signum);
static int restart_timer (void);
#endif
static void get_poli_config (void);
static void poli_sync (void);
static void poli_sync_node (void);

static void init_energy_reading (struct energy_reading *reading);

/******************************************************************************/
/*                      INITIALIZATION                                        */
/******************************************************************************/

int poli_init (void)
{
    monitor = malloc(sizeof(struct monitor_t));
    poli_config = malloc(sizeof(struct polimer_config_t));
    get_poli_config();
#ifndef _NOMPI
    mpi_init(monitor);
#else
    single_node_init(monitor);
#endif

#ifndef _NOOMP
    monitor->num_threads = omp_get_num_threads();
    monitor->thread_id = omp_get_thread_num();
#endif

    if (monitor->node_rank == 0 && monitor->thread_id == 0)
        monitor->imonitor = 1;

    get_jobid();


    if (monitor->imonitor)
    {
        poller = malloc(sizeof(struct poller_t));
        poller->time_counter = 0;

        //initialize the main struct
        init_system_info();
        //initialize all power monitoring and control interfaces
        init_power_interfaces(system_info);

        get_initial_time(system_info, monitor);

        // may not be relevant but here it is assumed that the system is at default settings
        if (get_system_power_caps(system_info, monitor) != 0)
            poli_log(ERROR, monitor, "Couldn't get power caps on init!");

        start_poli_tag_no_sync("application_summary");
        // record energy
        system_info->initial_energy = read_current_energy(system_info);
        get_power_info(&system_info->power_info, system_info);
    }

    poli_sync();

#ifdef _POWMGR
    init_power_manager(system_info, monitor, poli_config);
#endif

#ifndef _TIMER_OFF
    //setup and start timer
    if (monitor->imonitor)
        setup_timer();
#endif

    poli_log(TRACE, monitor,   "Finishing %s\n", __FUNCTION__);

    return 0;
}

static void get_poli_config (void)
{
    char *poll_interval = getenv("POLIMER_POLL_INTERVAL");
    if (poll_interval == NULL)
        poli_config->poll_interval = POLL_INTERVAL;
    else
        sscanf(poll_interval, "%f", &poli_config->poll_interval);
    
    char *user_loglevel = getenv("POLIMER_LOG_LEVEL");
    if (user_loglevel == NULL)
    {
#ifdef _DEBUG
        poli_config->log_level = DEBUG;
#elif _TRACE
        poli_config->log_level = TRACE;
#else
        poli_config->log_level = WARNING;
#endif
    }
    else
    {
        if (strcmp(user_loglevel, "DEBUG") == 0)
            poli_config->log_level = DEBUG;
        else if (strcmp(user_loglevel, "TRACE") == 0)
            poli_config->log_level = TRACE;
        else if (strcmp(user_loglevel, "WARNING") == 0)
            poli_config->log_level = WARNING;
        else if (strcmp(user_loglevel, "ERROR") == 0)
            poli_config->log_level = ERROR;
        else if (strcmp(user_loglevel, "INFO") == 0)
            poli_config->log_level = INFO;
        else
            poli_config->log_level = WARNING;
    }
    poli_setloglevel(poli_config->log_level);

    char *cap = getenv("POLIMER_CAP_SHORT");
    if (cap == NULL)
        poli_config->cap_short_window = 0;
    else
        poli_config->cap_short_window = atoi(cap);

#ifdef _POWMGR
    char *palloc_freq = getenv("POLIMER_POWER_ALLOC_FREQ");
    if (palloc_freq == NULL)
        poli_config->palloc_freq = 1;
    else
        poli_config->palloc_freq = atoi(palloc_freq);
    
    char *use_sync_window = getenv("POLIMER_SYNC_WINDOW_OFF");
    if (use_sync_window == NULL)
        poli_config->use_sync_window = 1;
    else
        poli_config->use_sync_window = 0;
    poli_config->policy = getenv("POLIMER_POLICY");
    if (poli_config->policy == NULL)
        poli_config->policy = "LAST_SYNC_POLLER_AVERAGE";
    char *measure_sync_end = getenv("POLIMER_MEASURE_SYNC_END");
    if (measure_sync_end == NULL)
        poli_config->measure_sync_end = 0;
    else
        poli_config->measure_sync_end = 1;
    char *simulate = getenv("POLIMER_SIMULATE_PM");
    if (simulate == NULL)
        poli_config->simulate_pm = 0;
    else
        poli_config->simulate_pm = atoi(simulate);
    poli_config->pm_algorithm = getenv("POLIMER_POWER_MANAGER");
    if (poli_config->pm_algorithm == NULL)
        poli_config->pm_algorithm = "SeeSAw";

    char *gp_delta = getenv("POLIMER_GEOPM_DELTA");
    if (gp_delta == NULL)
        poli_config->gp_delta = START_DELTA;
    else
        sscanf(gp_delta, "%lf", &poli_config->gp_delta);
#endif
}

static void get_jobid (void)
{
#ifdef _COBALT
    monitor->jobid = getenv("COBALT_JOBID");
#else
    monitor->jobid = getenv("PoLi_JOBNAME");
    if (monitor->jobid == NULL)
        strcpy(monitor->jobid, "myjob");
#endif  
}

static int single_node_init (struct monitor_t * monitor)
{
    monitor->world_size = 1;
    monitor->node_size = 0;
    monitor->color = 0;
    monitor->world_rank = 0;
    monitor->node_rank = 0;
#ifdef _COBALT
    monitor->my_host = getenv("COBALT_PARTNAME");
#else
    monitor->my_host = getenv("HOSTNAME");
#endif

    if (monitor->my_host == NULL)
        strcpy(monitor->my_host, "host");

    monitor->num_threads = 0;
    monitor->thread_id = 0;
    monitor->num_monitors = 1;

    return 0;
}

static void init_system_info (void)
{
    //allocate memory and set dummy values
    system_info = malloc(sizeof(struct system_info_t));

    system_info->poli_tag_list = 0;
    system_info->pcap_tag_list = 0;
    system_info->current_pcap_list = 0;

#ifndef _TIMER_OFF
    system_info->system_poll_list = 0;
#endif

#ifdef _POWMGR
    system_info->palloc_list = 0;
    system_info->palloc_list = calloc(MAX_POLL_SAMPLES, sizeof(struct power_manager_t));
    system_info->palloc_count = 0;
#endif

    system_info->num_poli_tags = 0;
    system_info->num_open_tags = 0;
    system_info->num_closed_tags = 0;

    system_info->poli_opentag_tracker = -1;
    system_info->poli_closetag_tracker = -1;

    system_info->num_pcap_tags = 0;

    // allocate list of poli tags (power measurements)
    system_info->poli_tag_list = calloc(MAX_TAGS, sizeof(struct poli_tag));

    // allocate list of power cap tags (create a tag each time we specifically set a power cap)
    system_info->pcap_tag_list = calloc(MAX_TAGS, sizeof(struct pcap_tag));

    //
    system_info->current_pcap_list = calloc(NUM_ZONES, sizeof(struct pcap_info));

#ifndef _TIMER_OFF
    //allocate list keeping the poll info
    system_info->system_poll_list = calloc(MAX_POLL_SAMPLES, sizeof(struct system_poll_info));
#endif

#ifdef _BENCH
    system_info->system_poll_list_em = calloc(MAX_POLL_SAMPLES, sizeof(struct system_poll_info));
#endif

    char *freq_path = "/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq";
    system_info->cur_freq_file = open(freq_path, O_RDONLY);

    if (system_info->cur_freq_file < 0)
    {
        poli_log(ERROR, monitor,   "Failed to open file to read frequency at %s!\n Error code: %s\n Trying cpuinfo_cur_frequency...", freq_path, strerror(errno));
        system_info->cur_freq_file = open("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_cur_freq", O_RDONLY);
        if (system_info->cur_freq_file < 0)
            poli_log(ERROR, monitor,   "Unable to access frequency on your system. Make sure you have read permission on cpuinfo_cur_freq.\n Error code: %s", strerror(errno));
    }
}


static void init_power_interfaces (struct system_info_t * system_info)
{
#ifdef _MSR
    //initialize the msr environment to read from/write to msrs
    init_msrs(system_info);
#endif
#ifdef _CRAY
    //initialize the cray environment to read the power monitoring counters
    init_cray_pm_counters (system_info);
#endif
}

/*                          END OF INITIALIZATION                             */

/******************************************************************************/
/*                      POWER MANAGEMENT                                      */
/******************************************************************************/

int poli_integrate_comm(int this_color, int app1_color, int app2_color, int my_app_rank, MPI_Group app_group, MPI_Comm app_comm)
{
#ifdef _POWMGR
    return integrate_communicators(monitor, this_color, my_app_rank, app_group, app_comm);
#else
    return 0;
#endif
}

void poli_sync_step_end(void)
{
#ifdef _POWMGR
    sync_step_end(monitor, system_info, poller);
#endif
}

void poli_update_timestep(int timestep)
{
#ifdef _POWMGR
    update_timestep(timestep);
#endif
}

int poli_get_sync_count(void)
{
#ifdef _POWMGR
    if (monitor->imonitor)
        return system_info->palloc_count;
    else
        return 0;
#else
    return 0;
#endif
}

void poli_palloc_off(void)
{
#ifdef _POWMGR
    power_manager_off();
#endif
}

void poli_start_palloc_meas(void)
{
#ifdef _POWMGR
    sync_end_measurements(system_info, monitor, poller);
#endif
}

void poli_end_palloc_meas(void)
{
#ifdef _POWMGR
    sync_start_measurements(system_info, monitor, poller);
#endif
}

void poli_set_palloc_freq(int freq)
{
#ifdef _POWMGR
    set_palloc_freq(freq, monitor);
#endif
}

int poli_palloc(double power_cap)
{
#ifdef _POWMGR
    return allocate_power(power_cap, system_info, monitor, poller);
#else
    return 0;
#endif
    
}

int poli_palloc_min_coll(double power_cap)
{
#ifdef _POWMGR
    return allocate_power_min_collectives(power_cap, system_info, monitor, poller);
#else
    return 0;
#endif
}

void poli_set_global_power_cap(double pcap)
{
#ifdef _POWMGR
    set_global_power_cap(pcap);
#endif
}

/*                          END OF POWER MANAGEMENT                           */


/******************************************************************************/
/*                      POLIMER TAGS                                          */
/******************************************************************************/

static struct poli_tag *find_poli_tag_for_name (char *tag_name)
{
    int tag_num;
    for (tag_num = 0; tag_num < system_info->num_poli_tags; tag_num++)
    {
        struct poli_tag *this_poli_tag = &system_info->poli_tag_list[tag_num];
        if (strcmp(this_poli_tag->tag_name, tag_name) == 0 && !this_poli_tag->closed) {
            return this_poli_tag;
        }
    }
    return 0;
}

static struct poli_tag *get_poli_tag_for_start_time_counter(int counter)
{
    if (monitor->imonitor)
    {
        int i;
        for (i = 0; i < system_info->num_poli_tags; i++)
        {
            struct poli_tag *this_poli_tag = &system_info->poli_tag_list[i];
            if (this_poli_tag->start_timer_count == counter)
                return this_poli_tag;
        }
    }
    return 0;
}

static struct poli_tag *get_poli_tag_for_end_time_counter(int counter)
{
    if (monitor->imonitor)
    {
        int i;
        for (i = 0; i < system_info->num_poli_tags; i++)
        {
            struct poli_tag *this_poli_tag = &system_info->poli_tag_list[i];
            if (this_poli_tag->end_timer_count == counter)
                return this_poli_tag;
        }
    }
    return 0;
}

int poli_start_tag (char *format_str, ...)
{
    char tag_name[TAG_NAME_LEN];
    va_list argptr;
    va_start(argptr, format_str);
    vsnprintf(tag_name, TAG_NAME_LEN, format_str, argptr);
    va_end(argptr);

    //poli_sync_node();
    poli_log(TRACE, monitor,   "Entering %s %s\n", __FUNCTION__, tag_name);
    int ret = start_poli_tag_no_sync(tag_name);
    poli_log(TRACE, monitor,   "Finishing %s %s\n", __FUNCTION__, tag_name);
    return ret;
}

static int start_poli_tag_no_sync (char *tag_name)
{
    if (monitor->imonitor)
    {
        int num_tags = system_info->num_poli_tags;
        struct poli_tag *new_poli_tag = &system_info->poli_tag_list[num_tags];
        new_poli_tag->id = num_tags;
        system_info->poli_opentag_tracker = num_tags;
        strncpy(new_poli_tag->tag_name, tag_name, TAG_NAME_LEN);
        new_poli_tag->monitor_id = monitor->color;
        new_poli_tag->monitor_rank = monitor->world_rank;

        new_poli_tag->start_energy = read_current_energy(system_info);

        new_poli_tag->start_time = get_time();
        gettimeofday(&(new_poli_tag->start_timestamp), NULL);
        new_poli_tag->start_timer_count = poller->time_counter;

        system_info->num_poli_tags++;
        system_info->num_open_tags++;
    }
    return 0;
}

int poli_end_tag (char *format_str, ...)
{
    char tag_name[TAG_NAME_LEN];
    va_list argptr;
    va_start(argptr, format_str);
    vsnprintf(tag_name, TAG_NAME_LEN, format_str, argptr);
    va_end(argptr);

    //poli_sync_node();
    poli_log(TRACE, monitor,   "Entering %s %s\n", __FUNCTION__, tag_name);
    int ret = end_poli_tag_no_sync(tag_name);
    poli_log(TRACE, monitor,   "Finishing %s %s\n", __FUNCTION__, tag_name);
    return ret;
}

static int end_poli_tag_no_sync (char *tag_name)
{
    int ret = 0;
    if (monitor->imonitor)
    {
        struct poli_tag *this_poli_tag = &system_info->poli_tag_list[system_info->poli_opentag_tracker];
        if (system_info->poli_opentag_tracker < 1) //1 because we don't want application_summary to be prematurely closed
        {
            if (strcmp(this_poli_tag->tag_name, tag_name) != 0)
            {
                poli_log(WARNING, monitor, "You attempted to close tag %s, but no tags were opened! This tag will be omitted", tag_name);
                return -1;
            }
        }

        //if (strcmp(this_poli_tag->tag_name, tag_name) != 0) //this enables interleaving tags
        //    this_poli_tag = find_poli_tag_for_name(tag_name);
        if (this_poli_tag == 0)
        {
            poli_log(ERROR, monitor,   "%s: Failed to find poli tag to end: %s\n", __FUNCTION__, tag_name);
            return 1;
        }
        //TD
        if (this_poli_tag->monitor_rank != monitor->world_rank)
            poli_log(WARNING, monitor, "%s tag is closed by rank %d which is different from the opening rank %d\n", tag_name, monitor->world_rank, this_poli_tag->monitor_rank);
        if (this_poli_tag->monitor_id != monitor->color)
            poli_log(WARNING, monitor, "%s tag is closed on node %d which is different from the opening node %d\n", tag_name, monitor->color, this_poli_tag->monitor_id);

        ret = end_existing_poli_tag(this_poli_tag);
    }
    return ret;
}

/*note: having a barrier here can cause problems if we have lots of ranks per node..*/
static int end_existing_poli_tag (struct poli_tag *this_poli_tag)
{
    if (monitor->imonitor)
    {
        poli_log(TRACE, monitor,   "Entering %s", __FUNCTION__);

        this_poli_tag->end_energy = read_current_energy(system_info);

        this_poli_tag->end_time = get_time();
        gettimeofday(&(this_poli_tag->end_timestamp), NULL);
        this_poli_tag->end_timer_count = poller->time_counter;
        this_poli_tag->closed = 1;
        system_info->poli_closetag_tracker = system_info->poli_opentag_tracker;
        system_info->num_closed_tags--; //yes, decrement
        system_info->poli_closetag_tracker = system_info->poli_opentag_tracker;
        system_info->poli_opentag_tracker--;

        poli_log(TRACE, monitor, "Finishing %s", __FUNCTION__);
    }
    return 0;
}

/*                      END OF EMON TAGS                                      */

/******************************************************************************/
/*                     SETTING POWER CAPS                                     */
/******************************************************************************/


//todo make this more template like
int poli_set_power_cap (double watts)
{
    return set_power_cap(watts, system_info, monitor, poller, poli_config);
}

int poli_set_power_cap_with_params(char *zone_name, double watts_long, double watts_short,
    double seconds_long, double seconds_short)
{
    return set_power_cap_with_params(zone_name, watts_long, watts_short, seconds_long,
        seconds_short, system_info, monitor, poller);
}

int poli_reset_system (void)
{
    return reset_system(system_info, monitor, poller);
}

/*                    END OF SETTING POWER CAPS                               */

/******************************************************************************/
/*                    GETTING POWER CAPS                                      */
/******************************************************************************/

int poli_get_power_cap_for_param (char *zone_name, char *param, double *result)
{
    return get_power_cap_for_param(zone_name, param, result, system_info, monitor);
}

int poli_get_power_cap (double *watts)
{
#ifdef _MSR
    return get_power_cap_package("watts_long", watts, system_info, monitor);
#else
    return 0;
#endif
}

int poli_get_power_cap_limits (char* zone_name, double *min, double *max)
{
    return get_power_cap_limits(zone_name, min, max, system_info, monitor);
}

void poli_print_power_cap_info (void)
{
    print_power_cap_info(system_info, monitor);
}

void poli_print_power_cap_info_verbose (void)
{
    print_power_cap_info_verbose(system_info, monitor);
}

struct power_info poli_get_power_info (void)
{
    struct power_info pi;
    get_power_info(&pi, system_info);
    return pi;
}

void poli_print_power_info (void)
{
    struct power_info pi = poli_get_power_info();
    printf("************************************************************\n");
    printf("                       POWER INFO                           \n");
    printf("                       RANK: %d NODE: %s                    \n", monitor->world_rank, monitor->my_host);
    printf("------------------------------------------------------------\n");

    printf("\tpackage thermal spec power: %lf\n", pi.package_thermal_spec);
    printf("\tpackage minimum power: %lf\n", pi.package_minimum_power);
    printf("\tpackage maximum power: %lf\n", pi.package_maximum_power);
    printf("\tpackage maximum time window: %lf\n", pi.package_maximum_time_window);
    printf("\tdram thermal spec power: %lf\n", pi.dram_thermal_spec);
    printf("\tdram minimum power: %lf\n", pi.dram_minimum_power);
    printf("\tdram maximum power: %lf\n", pi.dram_maximum_power);
    printf("\tdram maximum time window: %lf\n", pi.dram_maximum_time_window);
}


/*               END OF GETTING POWER CAPS                                    */

/******************************************************************************/
/*              FREQUENCY                                                     */
/******************************************************************************/

int poli_get_current_frequency (double *freq)
{
    return get_current_frequency_value(system_info, monitor, freq, poller);
}

int poli_print_frequency_info (void)
{
    print_frequency_info(system_info, monitor, poller);
}

/*               END OF FREQUENNCY                                            */

/******************************************************************************/
/*              TIMER                                                         */
/******************************************************************************/

#ifndef _TIMER_OFF
static int setup_timer (void)
{
    if (monitor->imonitor)
    {
        poller->timer_on = 1;
        if (!poli_config->poll_interval)
        {
            poller->timer_on = 0;
            return 0;
        }

        memset(&poller->sa, 0, sizeof(poller->sa));
        poller->sa.sa_handler = &timer_handler;

        int status = sigaction(SIGALRM, &poller->sa, NULL);
        if (0 != status)
        {
            poli_log(ERROR, monitor,   "Failed to set SIGACTION: %s", strerror(errno));
            return 1;
        }

        poller->timer.it_value.tv_sec = 0;
        poller->timer.it_value.tv_usec = INITIAL_TIMER_DELAY;

        poller->timer.it_interval.tv_sec = 0;
        poller->timer.it_interval.tv_usec = poli_config->poll_interval * 1000000;

        status = setitimer(ITIMER_REAL, &poller->timer, NULL);
        if (0 != status)
        {
            poli_log(ERROR, monitor,   "Failed to set timer: %s", strerror(errno));
            return 1;
        }
    }
    return 0;
}

static int restart_timer (void)
{
    poller->timer_on = 1;

    int status = sigaction(SIGALRM, &poller->sa, NULL);
    if (0 != status)
    {
        poli_log(ERROR, monitor,   "Failed to set SIGACTION: %s", strerror(errno));
        return 1;
    }

    poller->timer.it_value.tv_sec = 0;
    poller->timer.it_value.tv_usec = INITIAL_TIMER_DELAY;

    poller->timer.it_interval.tv_sec = 0;
    poller->timer.it_interval.tv_usec = POLL_INTERVAL * 1000000;

    status = setitimer(ITIMER_REAL, &poller->timer, NULL);
    if (0 != status)
    {
        poli_log(ERROR, monitor,   "Failed to set timer: %s", strerror(errno));
        return 1;
    }

    return 0;
}

static int stop_timer (void)
{
    poller->timer_on = 0;
    sigaction(SIGALRM, &poller->sa, NULL);

    poller->timer.it_value.tv_sec = 0;
    poller->timer.it_value.tv_usec = 0;

    poller->timer.it_interval.tv_sec = 0;
    poller->timer.it_interval.tv_usec = 0;

    setitimer(ITIMER_REAL, &poller->timer, NULL);
    return 0;
}

static void init_energy_reading (struct energy_reading *reading)
{
#ifdef _MSR
    struct rapl_energy rapl_energy = {0};
    reading->rapl_energy = rapl_energy;
#elif _CRAY
    struct cray_measurement cray_meas = {0};
    reading->cray_meas = cray_meas;
#elif _BGQ
    struct bgq_measurement bgq_meas = {0};
    reading->bgq_meas = bgq_meas;
#endif
    return;
}

static void timer_handler (int signum)
{
    if (poller->timer_on && monitor->imonitor)
    {
        if (poller->time_counter < MAX_POLL_SAMPLES)
        {
            struct system_poll_info *info = &system_info->system_poll_list[poller->time_counter];
            info->current_energy = read_current_energy(system_info);
            info->wtime = get_time();
            struct energy_reading last_energy = system_info->initial_energy;

            if (poller->time_counter > 0)
            {
                int last_found = 0;
                int i;
                for (i = poller->time_counter - 1; i >= 0; i--)
                {
                    if (system_info->system_poll_list[i].wtime)
                    {
                        last_energy = system_info->system_poll_list[i].current_energy;
                        //overflow
                        if (info->wtime < system_info->system_poll_list[i].wtime)
                            info->time_diff = (double) poli_config->poll_interval; //best approximation
                        else
                            info->time_diff = info->wtime - system_info->system_poll_list[i].wtime;
                        last_found = 1;
                        break;
                    }
                }

                if (!last_found)
                    info->time_diff = (double) poli_config->poll_interval;
                
            }
            else
            {
                info->time_diff = info->wtime - system_info->initial_mpi_wtime;
            }

            if (!info->time_diff)
                info->time_diff = (double) poli_config->poll_interval;

#ifdef _MSR
            int zone;
            for (zone = 0; zone < system_info->sysmsr->num_zones; zone++)
            {
                info->pcap_info_list[zone] = system_info->current_pcap_list[zone];
            }
#endif

            info->counter = poller->time_counter;

            info->pkg_pcap = system_info->current_pcap_list[PACKAGE_INDEX].watts_long;
            //get_current_frequency_info(system_info, monitor, info, poller);
            info->last_energy = last_energy;

            compute_current_power(info, info->time_diff, system_info);
            //poli_log(TRACE, NULL, "%s : current E: %" PRIu64 ", time diff : %f\n", __FUNCTION__, info->current_energy.rapl_energy.package, info->time_diff);
            info->free_power = info->pkg_pcap - info->computed_power.rapl_power.package;
            info->power_util = info->computed_power.rapl_power.package / info->pkg_pcap;

            poller->time_counter++;
        }
    }
    return;
}
#endif


/*               END OF TIMER                                                 */

/******************************************************************************/
/*              HELPERS                                                       */
/******************************************************************************/


static void poli_sync (void)
{
#ifndef _NOMPI
    barrier(monitor);
#endif
    return;
}

static void poli_sync_node (void)
{
#ifndef _NOMPI
    barrier_node(monitor);
#endif
    return;
}

#ifndef _NOMPI
int poli_am_monitor (void)
{
    if (monitor->imonitor)
        return 1;
    else
        return 0;
}

int poli_get_monitor_id (int *id)
{
    if (monitor->imonitor)
        (*id) = monitor->world_rank;
    else
        (*id) = -1;
    return 0;
}

int poli_get_monitor_node (int *node_id)
{
    if (monitor->imonitor)
        (*node_id) = atoi(monitor->my_host);
    else
        (*node_id) = -1;
    return 0;
}

int poli_get_subcommunicator (MPI_Comm *comm)
{
    *comm = monitor->mynode_comm;
    return 0;
}

int poli_get_num_monitors (void)
{
    int num_monitors;
    MPI_Allreduce(&monitor->imonitor, &num_monitors, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
    return num_monitors;
}
#endif



/*                  END OF HELPERS                                            */

/******************************************************************************/
/*                  FINALIZING/CLEANUP                                        */
/******************************************************************************/



static void finalize_power_interfaces (struct system_info_t * system_info)
{
    finalize_msrs(system_info);
#ifdef _CRAY
    finalize_cray_pm_counters(system_info);
#endif
    return;
}

static int finalize_tags (void)
{
    if (system_info->num_open_tags + system_info->num_closed_tags == 0)
        return 0;

    poli_log(WARNING, monitor, "There are unfinished tags! Attempting to close them...");

    int tag_num;

    for (tag_num = 0; tag_num < system_info->num_poli_tags; tag_num++)
    {
        struct poli_tag *this_poli_tag = &system_info->poli_tag_list[tag_num];
        if ( this_poli_tag->closed == 0)
        {
            poli_log(WARNING, monitor, "Tag %s is not finished. It will be closed automatically.", this_poli_tag->tag_name);
            end_existing_poli_tag(this_poli_tag);
        }
    }

    return 0;
}

int poli_finalize(void)
{
    if (monitor->imonitor)
    {
        struct poli_tag *app_summary = &system_info->poli_tag_list[0]; //need to close application summary tag which is the first one
        end_existing_poli_tag(app_summary);
    }
    poli_sync();

    poli_log(TRACE, monitor, "Entering %s", __FUNCTION__);

    if (monitor->imonitor)
    {
        poli_log(TRACE, monitor, "Finalizing: Getting application power, energy and time summary");

#ifdef _POWMGR
        finalize_power_manager(system_info, monitor);
#endif
        poli_log(TRACE, monitor, "Checking if any poli tags are unfinished");

        /* Check if any poli tags are unfinished */
        finalize_tags();

        poli_log(TRACE, monitor, "Resetting the system");

        /* Reset system power caps */
        if (poli_reset_system() != 0)
            if (!system_info->sysmsr->error_state)
                poli_log(ERROR, monitor, "Couldn't reset system!");

#ifndef _TIMER_OFF
        poli_log(TRACE, monitor, "Stopping timer");
        stop_timer();
#endif
        poli_log(TRACE, monitor, "Pushing results to file");
        file_handler(system_info, monitor, poller);

        poli_log(TRACE, monitor,   "Closing frequency file");
        if (system_info->cur_freq_file)
            close(system_info->cur_freq_file);

        poli_log(TRACE, monitor,   "Cleaning up structures");

        /* Cleanup */
        if (system_info->poli_tag_list)
        {
            free(system_info->poli_tag_list);
            system_info->poli_tag_list = 0;
        }
        if (system_info->pcap_tag_list)
        {
            free(system_info->pcap_tag_list);
            system_info->pcap_tag_list = 0;
        }
        if (system_info->current_pcap_list) //this must be done after system reset
        {
            free(system_info->current_pcap_list);
            system_info->current_pcap_list = 0;
        }
#ifndef _TIMER_OFF
        if (system_info->system_poll_list)
        {
            free(system_info->system_poll_list);
            system_info->system_poll_list = 0;
        }
#endif
#ifdef _BENCH
        if (system_info->system_poll_list_em)
        {
            free(system_info->system_poll_list_em);
            system_info->system_poll_list_em = 0;
        }
#endif

        finalize_power_interfaces(system_info);

        if (poller)
            free(poller);

        if (system_info)
            free(system_info);

    }

#ifndef _NOMPI
    if (!is_finalized())
        MPI_Comm_free(&monitor->mynode_comm);
#endif

    if (monitor)
        free(monitor);

    poli_log(TRACE, monitor,   "Finishing %s", __FUNCTION__);

    return 0;
}

/*                   END OF FINALIZING/CLEANUP                                */
