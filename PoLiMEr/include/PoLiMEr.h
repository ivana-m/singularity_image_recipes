#ifndef __POLIMER_H
#define __POLIMER_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <sys/types.h>
#include <sys/time.h>
#include <stdint.h>
#include <signal.h>
#include <time.h>

#ifndef _NOMPI
#include "mpi.h"
#endif

#include "msr_handler.h"

#ifdef _CRAY
#include "cray_handler.h"
#endif

#ifdef _BGQ
#include "bgq_handler.h"
#endif


// Maximum number of user-specified tags
#define MAX_TAGS     10000
// Maximum number of polling records
#define MAX_POLL_SAMPLES 500000
#define POLL_INTERVAL 0.2
#define INITIAL_TIMER_DELAY 100000
#define TAG_NAME_LEN 500

struct monitor_t {
    int imonitor;
    int world_rank;
    int world_size;
    int node_rank;
    int node_size;
    int color;
    int num_threads;
    int thread_id;
    char *jobid;
    int num_monitors;
#ifndef _NOMPI
    MPI_Comm mynode_comm;
    //char my_host[MPI_MAX_PROCESSOR_NAME];
    MPI_Comm monitors_comm;
    int monitors_size;
    MPI_Comm sa_comm;
    MPI_Group monitors_group;
    MPI_Group sa_group;
    MPI_Comm sa_master_comm;
    int sa_rank;
    int sa_size;
    int sa_master;
    int sa_master_size;
    int sa_master_rank;

    MPI_Comm sa_allcomm;
//#else
    //char *my_host;
#endif
    char *my_host;
};

struct poller_t {
    int time_counter;
#ifdef _BENCH
    int time_counter_em;
#endif
#ifndef _TIMER_OFF
    struct sigaction sa;
    struct itimerval timer;
    volatile int timer_on;
#endif
};


struct energy_reading {
#ifdef _MSR
  struct rapl_energy rapl_energy;
#endif
#if _CRAY
  struct cray_measurement cray_meas;
#elif _BGQ
  struct bgq_measurement bgq_meas;
#else
  int placeholder;
#endif
};

struct power_reading {
#ifdef _MSR
  struct rapl_power rapl_power;
#endif
#if _CRAY
  struct cray_measurement cray_meas;
#elif _BGQ
  struct bgq_measurement bgq_meas;
#else
  int placeholder;
#endif
};

struct poli_tag {
    int id;
    char tag_name[TAG_NAME_LEN];
    int monitor_id;
    int monitor_rank;

    struct energy_reading start_energy;
    struct energy_reading end_energy;
    struct energy_reading total_energy;
    struct power_reading total_power;

    double start_time;
    double end_time;
    struct timeval start_timestamp;
    struct timeval end_timestamp;
    int start_timer_count;
    int end_timer_count;
    int closed;
};

typedef enum pcap_flags { DEFAULT, USER_SET, SYSTEM_RESET, INTERNAL, INITIAL } pcap_flag_t;

struct pcap_tag {
    int id;
    int monitor_id;
    int monitor_rank;
    char zone[ZONE_NAME_LEN];
    double watts_long;
    double watts_short;
    double seconds_long;
    double seconds_short;
    int enabled;
    double wtime;
    struct timeval timestamp;
    pcap_flag_t pcap_flag; //to have some idea if system reset, user set or controlled by library
    struct poli_tag active_poli_tags[MAX_TAGS]; //stores all active tags
    int num_active_poli_tags;
    int start_timer_count;
};

struct pcap_info {
    int monitor_id;
    int monitor_rank;
    zone_label_t zone_label;
    char zone[ZONE_NAME_LEN];
    int enabled_long;
    int enabled_short;
    int clamped_long;
    int clamped_short;
    double watts_long;
    double watts_short;
    double seconds_long;
    double seconds_short;
    double thermal_spec;
    double min;
    double max;
    double max_time_window;
};

struct polimer_config_t {
    float poll_interval;
    int log_level;
    int cap_short_window;
#ifdef _POWMGR
    int measure_sync_end;
    int simulate_pm;
    int palloc_freq;
    int use_sync_window;
    char *policy;
    char *pm_algorithm;
    double gp_delta;
#endif
};

struct frequency {
    double freq;
#ifdef _CRAY
    double cray_freq;
#endif
};

struct system_poll_info {
    int counter;
    double pkg_pcap;
    double free_power;
    double power_util;
    struct pcap_info pcap_info_list[NUM_ZONES];
    struct energy_reading last_energy;
    struct energy_reading current_energy;
    struct power_reading computed_power;
    struct frequency freq;
    double wtime;
    double time_diff;
};

#ifdef _POWMGR
struct power_manager_t;
#endif

struct system_info_t {

    /* record initial start times */
    struct timeval initial_start_time;
    double initial_mpi_wtime;

    /*record initial energy*/
    struct energy_reading initial_energy;
    struct energy_reading final_energy;

    struct poli_tag *poli_tag_list;
    int poli_opentag_tracker;
    int poli_closetag_tracker;
    struct pcap_tag *pcap_tag_list;
    struct pcap_info *current_pcap_list; //stores PACKAGE, CORE, DRAM in that order

#ifndef _TIMER_OFF
    struct system_poll_info *system_poll_list;
#endif
#ifdef _BENCH
    struct system_poll_info *system_poll_list_em;
#endif

#ifdef _POWMGR
    struct power_manager_t *palloc_list;
    int palloc_count;
#endif

    int num_poli_tags;
    int num_open_tags;
    int num_closed_tags;
    int num_pcap_tags;

    int cur_freq_file;

    /* add all system-dependent structs here*/
#ifdef _MSR
    struct system_msr_info *sysmsr;
    struct power_info power_info;
#endif
#ifdef _CRAY
    struct system_cray_info *syscray;
#endif
};



/******************************************************************************/
/*                      INITIALIZATION                                        */
/******************************************************************************/
/* emom_init - initalizes the necessary MPI and Energymon environments. must be called after MPI_Init() in application
   returns: 0 if everything went well*/
int poli_init(void);

/*                      END OF INITIALIZATION                                 */

/******************************************************************************/
/*                      EMON TAGS                                             */
/******************************************************************************/

/* start_poli_tag - starts an poli tag
   input: tag name
   returns: 0 if no errors, 1 otherwise*/
//int poli_start_tag(char *tag_name);
int poli_start_tag(char *format_str, ...);

/* end_poli_tag - ends an poli tag
   input: name of tag to end
   returns: 0 if no errors, 1 otherwises*/
int poli_end_tag(char *format_str, ...);


/*                      END OF EMON TAGS                                      */


/******************************************************************************/
/*                     SETTING POWER CAPS                                     */
/******************************************************************************/

/* poli_set_power_cap - sets a general power cap (uses PACKAGE zone as default)
   input: desired power in watts
   returns: 0 if no errors, 1 otherwise*/
int poli_set_power_cap (double watts);

int poli_set_power_cap_with_params(char *zone_name, double watts_long, double watts_short, double seconds_long, double seconds_short);

/* poli_reset_system - resets the system power caps to default values, which also creates a pcap tag
   returns: 0 if no errors, 1 otherwise*/
int poli_reset_system(void);

/*                    END OF SETTING POWER CAPS                               */

/******************************************************************************/
/*                    GETTING POWER CAPS                                      */
/******************************************************************************/

/* poli_get_power_cap - returns the package power cap as default
   input: pointer to double holding the resulting watts
   returns: 0 if successful, -1 otherwise*/
int poli_get_power_cap (double *watts);

/* poli_get_power_cap - returns power cap (watts_long) last recorded by a process (not executing system call for better performance)
   input: the name of the zone for which power cap is requested, pointer to double holding the resulting watts
   returns: 0 if successful, -1 otherwise*/
int poli_get_power_cap_for_param (char *zone_name, char *param, double *result);

int poli_get_power_cap_limits (char *zone_name, double *min, double *max);

void poli_print_power_cap_info (void);
/* print_power_cap_info - prints all information related to currently recorded power caps*/
void poli_print_power_cap_info_verbose (void);


/*               END OF GETTING POWER CAPS                                    */

/******************************************************************************/
/*              FREQUENCY                                                     */
/******************************************************************************/

int poli_get_current_frequency (double *freq);
int poli_print_frequency_info (void);

/*               END OF FREQUENNCY                                            */

/******************************************************************************/
/*              HELPERS                                                       */
/******************************************************************************/

#ifndef _NOMPI
int poli_am_monitor (void);
int poli_get_monitor_id (int *id);
int poli_get_monitor_node (int *node_id);
int poli_get_subcommunicator (MPI_Comm *comm);
/* get_num_monitors - returns the total number of monitors */
int poli_get_num_monitors (void);
#endif


/*                  END OF HELPERS                                            */

/******************************************************************************/
/*                  FINALIZING/CLEANUP                                        */
/******************************************************************************/

/* poli_finalize - manages output of results, and performs general cleanup
   returns: 0*/
int poli_finalize(void);

/*                   END OF FINALIZING/CLEANUP                                */

int poli_integrate_comm(int this_color, int app1_color, int app2_color, int my_app_rank, MPI_Group app_group, MPI_Comm app_comm);
int poli_palloc(double power_cap);
void poli_sync_step_end(void);
struct power_info poli_get_power_info (void);
void poli_print_power_info (void);
int poli_get_sync_count(void);

void poli_set_global_power_cap(double pcap);

void poli_update_timestep(int timestep);

void poli_palloc_off(void);

int poli_palloc_min_coll(double power_cap);

void poli_start_palloc_meas(void);
void poli_end_palloc_meas(void);

void poli_set_palloc_freq(int freq);

#ifdef __cplusplus
}
#endif

#endif
