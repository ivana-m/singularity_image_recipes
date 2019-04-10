#ifndef __FREQUENCY_HANDLER_H
#define __FREQUENCY_HANDLER_H

#ifdef __cplusplus
extern "C"
{
#endif

struct system_info_t;
struct monitor_t;
struct poller_t;
struct system_poll_into;

int get_current_frequency_value (struct system_info_t * system_info, struct monitor_t * monitor, double *freq, struct poller_t * poller);
int print_frequency_info (struct system_info_t * system_info, struct monitor_t * monitor, struct poller_t * poller);
int get_current_frequency_info (struct system_info_t * system_info, struct monitor_t * monitor, struct system_poll_info * info, struct poller_t * poller);

#ifdef __cplusplus
}
#endif

#endif