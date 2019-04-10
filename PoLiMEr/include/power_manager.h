#ifndef __POWER_MANAGER_H
#define __POWER_MANAGER_H

#ifdef __cplusplus
extern "C"
{
#endif

#define MAX_PM_LOGS 500000
#define MEASURABLE_POWER_UPPER 258
#define MEASURABLE_POWER_LOWER 80

#define PCAP_DELTA 2.5
#define RUNTIME_THRESH 0.01
#define MIN_DELTA 0.125
#define START_DELTA 8

#define SLURM_BALANCE_INTERVAL 30
#define SLURM_DECREASE_RATE 0.1
#define SLURM_INCREASE_RATE 0.1
#define SLURM_LOWER_THRESH 0.95
#define SLURM_UPPER_THRESH 0.95

struct system_info_t;
struct monitor_t;
struct energy_reading;
struct poller_t;
struct polimer_config_t;

struct pm_log_t {
    int palloc_count;
    int pm_count;
    int timestep;
    int world_rank;

    double my_alpha;
    double other_alpha;
    double ratio;
    double my_opt_power;
    double adjusted_opt_power;
    double other_opt_power;

    double allocated_power;
    double new_power_per_node;
    double previous_total_power;
    
    double max_observed_power;
    double observed_power_all_nodes;
    double pcap_all_nodes;

    double start_time;
    double end_time;
    double average_time;
    double time_allocated;

    double slack_power;
    double extra_power;
    int at_pcap;

    double median_node_runtime;
    int median_runtime_rank;
    double target_runtime;
    double delta;
    int target_met;
    
};

struct power_manager_t {
    int count;
    int log_count;
    int last_sync;
    int sync_begin;
    double time_after_alloc;
    double median_last_sync_time;
    double median_current_time;
    double last_sync_time;
    double my_last_sync_time;
    double current_time;
    double my_current_time;
    double first_to_enter_rank;
    double first_to_enter_node;
    double last_to_exit_rank;
    double last_to_exit_node;
    struct energy_reading last_energy;
    struct energy_reading current_energy;
    struct energy_reading total_energy;
    struct power_reading total_power;
    double average_poll_power;
    double average_poll_energy;
    double median_poll_power;
    double max_poll_power;
    double new_power_node;
    double opt_power_node;
    double global_pcap;
    int timestep;
    int freq;
    int off;

    struct rapl_power total_poll_power;
    struct rapl_energy total_poll_energy;
    double time_sum;
    double power_sum;

    // int last_node;
    // int last_rank;
    // int first_node;
    // int first_rank;
    int node;
    int rank;
    double pcap;

    int use_sync_window;
    int measure_sync_end;
    int simulate;
    char *policy;
    char *pm_algorithm;
    struct pm_log_t *logs;
    int SeeSAw;
    int SLURMLike;
    int GEOPMLike;
    double delta;
    int target_met;
};



void init_power_manager (struct system_info_t * system_info, struct monitor_t * monitor, struct polimer_config_t * poli_config);
void sync_step_end(struct monitor_t * monitor, struct system_info_t * system_info, struct poller_t * poller);
void sync_start_measurements(struct system_info_t * system_info, struct monitor_t * monitor, struct poller_t * poller);
void set_palloc_freq (int freq, struct monitor_t * monitor);
void update_timestep(int timestep);
void get_outlier_ranks (double local_time, double global_time, int * rankID, int * nodeID, struct monitor_t * monitor);
void sync_end_measurements(struct system_info_t * system_info, struct monitor_t * monitor, struct poller_t * poller);
int allocate_power(double power_cap, struct system_info_t * system_info, struct monitor_t * monitor, struct poller_t * poller);
int allocate_power_min_collectives(double power_cap, struct system_info_t * system_info, struct monitor_t * monitor, struct poller_t * poller);
void finalize_power_manager(struct system_info_t * system_info, struct monitor_t * monitor);
int integrate_communicators(struct monitor_t * monitor, int this_color, int my_app_rank, MPI_Group app_group, MPI_Comm app_comm);
int SeeSAw(double power_cap, struct system_info_t * system_info, struct monitor_t * monitor, struct poller_t * poller);
int SLURMLike(double power_cap, struct system_info_t * system_info, struct monitor_t * monitor, struct poller_t * poller);
int GEOPMLike(double power_cap, struct system_info_t * system_info, struct monitor_t * monitor, struct poller_t * poller);

void finalize_SeeSAw (struct system_info_t * system_info, struct monitor_t * monitor);
void finalize_sync_measurements (struct system_info_t * system_info, struct monitor_t * monitor);
void finalize_SLURMLike (struct system_info_t * system_info, struct monitor_t * monitor);
void finalize_GEOPMLike (struct system_info_t * system_info, struct monitor_t * monitor);

#ifdef __cplusplus
}
#endif

#endif