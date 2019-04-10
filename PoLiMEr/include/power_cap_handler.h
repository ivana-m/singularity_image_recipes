#ifndef __POWER_CAP_HANDLER_H
#define __POWER_CAP_HANDLER_H

#ifdef __cplusplus
extern "C"
{
#endif

struct system_info_t;
struct monitor_t;
struct pcap_tag;
struct pcap_info;
struct poller_t;
struct polimer_config_t;


int get_zone_index (char *zone_name, char * zone_names[], int zone_names_len[], struct system_info_t * system_info);
int set_power_cap (double watts, struct system_info_t * system_info,
    struct monitor_t * monitor, struct poller_t * poller, struct polimer_config_t * poli_config);
int set_power_cap_with_params(char *zone_name, double watts_long, double watts_short,
    double seconds_long, double seconds_short, struct system_info_t * system_info,
    struct monitor_t * monitor, struct poller_t * poller);
int reset_system (struct system_info_t * system_info, struct monitor_t * monitor, struct poller_t * poller);
int get_power_cap_for_param (char *zone_name, char *param, double *result,
    struct system_info_t * system_info, struct monitor_t * monitor);
int get_power_cap (double *watts, struct system_info_t * system_info, struct monitor_t * monitor);
int get_power_cap_limits (char* zone_name, double *min, double *max,
    struct system_info_t * system_info, struct monitor_t * monitor);
void print_power_cap_info (struct system_info_t * system_info, struct monitor_t * monitor);
void print_power_cap_info_verbose (struct system_info_t * system_info, struct monitor_t * monitor);
int get_power_cap_package (char *param, double *result,
    struct system_info_t * system_info, struct monitor_t * monitor);
char * get_zone_name_by_index(int index);
int get_system_power_caps (struct system_info_t * system_info, struct monitor_t * monitor);

#ifdef __cplusplus
}
#endif

#endif
