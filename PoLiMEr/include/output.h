#ifndef __OUTPUT_H
#define __OUTPUT_H

#ifdef __cplusplus
extern "C"
{
#endif

struct system_info_t;
struct monitor_t;
struct poli_tag;
struct pcap_tag;
struct poller_t;
struct system_poll_info;

#ifdef _MSR
struct rapl_energy;
#endif

#ifdef _CRAY
struct cray_measurement;
#endif

#ifdef _BGQ
struct bgq_measurement;
#endif

int file_handler (struct system_info_t * system_info, struct monitor_t * monitor, struct poller_t * poller);
int poli_tags_to_file (struct system_info_t * system_info, struct monitor_t * monitor);
int pcap_tags_to_file (struct system_info_t * system_info, struct monitor_t * monitor);
int polling_info_to_file (struct system_info_t * system_info, struct monitor_t * monitor, struct poller_t * poller);


#ifdef __cplusplus
}
#endif

#endif