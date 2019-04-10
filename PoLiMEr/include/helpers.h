#ifndef __HELPERS_H
#define __HELPERS_H

#ifdef __cplusplus
extern "C"
{
#endif

struct system_poll_info;
struct system_info_t;
struct monitor_t;

double get_time (void);
void get_initial_time(struct system_info_t * system_info, struct monitor_t * monitor);
int compute_current_power (struct system_poll_info * info, double time, struct system_info_t * system_info);
struct energy_reading read_current_energy (struct system_info_t * system_info);
void get_timestamp(double time_from_start, char *time_str_buffer, size_t buff_len, struct timeval * initial_start_time);
FILE * open_file (char *filename, struct monitor_t * monitor);
int coordsToInt (int *coords, int dim);

#ifdef __cplusplus
}
#endif

#endif