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

#include "PoLiLog.h"
#include "output.h"
#include "helpers.h"

#ifdef _POWMGR
#include "power_manager.h"
#endif

#ifdef _MSR
#include "msr_handler.h"
#include "power_cap_handler.h"
#endif

#ifdef _CRAY
#include "cray_handler.h"
#endif

#ifdef _BGQ
#include "bgq_handler.h"
#endif

static void get_poli_tags_for_time_counter(FILE *fp, int counter, struct system_info_t * system_info, struct monitor_t * monitor);
static void get_pcap_tag_for_time_counter(FILE *fp, int counter, struct system_info_t * system_info, struct monitor_t * monitor);

int file_handler (struct system_info_t * system_info, struct monitor_t * monitor, struct poller_t * poller)
{
    int ret = 0;
    if (monitor->imonitor)
    {
        if (polling_info_to_file(system_info, monitor, poller) != 0)
        {
            ret = 1;
            poli_log(ERROR, monitor,   "Something went wrong with writing polling output to file");
        }
        if (system_info->num_poli_tags > 0)
        {
            if (poli_tags_to_file(system_info, monitor) != 0)
            {
                ret = (ret || 1);
                poli_log(ERROR, monitor,   "Something went wrong with writing energy tags to file\n");
            }
        }
        if (system_info->num_pcap_tags > 0)
        {
            if (pcap_tags_to_file(system_info, monitor) != 0)
            {
                ret = (ret || 1);
                poli_log(ERROR, monitor,   "Something went wrong with \n");
            }
        }
    }
    return ret;
}

static void get_pcap_tag_for_time_counter(FILE *fp, int counter, struct system_info_t * system_info, struct monitor_t * monitor)
{
    if (monitor->imonitor)
    {
        int i;
        for (i = 0; i < system_info->num_pcap_tags; i++)
        {
            struct pcap_tag * tag = &system_info->pcap_tag_list[i];
            if (tag->start_timer_count == counter)
            {
                fprintf(fp, "*** SET POWER CAP TAG %d TO: %s, %lf\n",
                        tag->id, tag->zone, tag->watts_long);
            }
        }
    }
    return;
}

static void get_poli_tags_for_time_counter(FILE *fp, int counter, struct system_info_t * system_info, struct monitor_t * monitor)
{
    if (monitor->imonitor)
    {
        int i;
        for (i = 0; i < system_info->num_poli_tags; i++)
        {
            struct poli_tag *this_poli_tag = &system_info->poli_tag_list[i];
            if (this_poli_tag->start_timer_count == counter && this_poli_tag->end_timer_count == counter)
            {
                fprintf(fp, "--- TAG START: %s\n", this_poli_tag->tag_name);
                fprintf(fp, "--- TAG END: %s\n", this_poli_tag->tag_name);
            }
            else if (this_poli_tag->start_timer_count == counter)
                fprintf(fp, "--- TAG START: %s\n", this_poli_tag->tag_name);
            else if (this_poli_tag->end_timer_count == counter)
                fprintf(fp, "--- TAG END: %s\n", this_poli_tag->tag_name);
        }
    }
    return;
}

int poli_tags_to_file (struct system_info_t * system_info, struct monitor_t * monitor)
{
    if (monitor->imonitor)
    {
        FILE *fp = open_file("PoLiMEr_energy-tags", monitor);
        if (fp == NULL)
            return 1;

#ifdef _POWMGR
        int iamsimulation = -1;
#endif

#ifndef _HEADER_OFF
        fprintf(fp, "Tag Name\tTimestamp\tStart Time (s)\tEnd Time (s)\tTotal Time (s)\t");
#ifdef _MSR
        if (!system_info->sysmsr->error_state)
        {
            fprintf(fp, "Total RAPL pkg E (J)\tTotal RAPL PP0 E (J)\tTotal RAPL PP1 E (J)\tTotal RAPL platform E (J)\tTotal RAPL dram E (J)\t");
            fprintf(fp, "Total RAPL pkg P (W)\tTotal RAPL PP0 P (W)\tTotal RAPL PP1 P (W)\tTotal RAPL platform P (W)\tTotal RAPL dram P (W)");
        }
#endif
#ifdef _CRAY
        fprintf(fp, "\tTotal Cray node E (J)\tTotal Cray cpu E (J)\tTotal Cray memory E (J)\t");
        fprintf(fp, "Total Cray node P (W)\tTotal Cray cpu P (W)\tTotal Cray memory P (W)\t");
        fprintf(fp, "Total Cray node calc P (W)\tTotal Cray cpu calc P (W)\tTotal Cray memory calc P (W)");
#endif
#ifdef _BGQ
        write_bgq_header(&fp);
#endif
        fprintf(fp, "\tRank\tNode");
        fprintf(fp, "\n");
#endif
        int tag_num;
        for (tag_num = 0; tag_num < system_info->num_poli_tags; tag_num++)
        {
            struct poli_tag *tag = &system_info->poli_tag_list[tag_num];

            double total_time = tag->end_time - tag->start_time;
            double start_offset, end_offset = 0.0;
            if (strcmp(tag->tag_name, "application_summary") == 0 && total_time < 0)
            {
                total_time = get_time() - system_info->initial_mpi_wtime;
                end_offset = total_time;
            }

#ifdef _POWMGR
            if (monitor->sa_master && iamsimulation == -1)
            {
                if (strncmp(tag->tag_name, "S-", 2) == 0)
                    iamsimulation = 1;
                if (strncmp(tag->tag_name, "A-", 2) == 0)
                    iamsimulation = 0;
            }
#endif

            compute_power_from_tag(tag, total_time);

            if (strcmp(tag->tag_name, "application_summary") == 0)
            {
                start_offset = 0.0;
                if (end_offset == 0.0)
                    end_offset = tag->end_time - tag->start_time;
            }
            else
            {
                start_offset = tag->start_time - system_info->initial_mpi_wtime;
                end_offset = tag->end_time - system_info->initial_mpi_wtime;
            }

            char time_str_buffer[20];
            get_timestamp(start_offset, time_str_buffer, sizeof(time_str_buffer), &system_info->initial_start_time);

            fprintf(fp, "%s\t%s\t%lf\t%lf\t%lf\t", tag->tag_name, time_str_buffer, start_offset, end_offset, total_time);

#ifdef _MSR
            if (!system_info->sysmsr->error_state)
            {
                struct rapl_energy total_energy = tag->total_energy.rapl_energy;
                struct rapl_power total_power = tag->total_power.rapl_power;
                //fprintf(fp, "%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t", total_energy.package, total_energy.pp0, total_energy.pp1, total_energy.platform, total_energy.dram);
                fprintf(fp, "%lf\t%lf\t%lf\t%lf\t%lf\t", total_energy.package, total_energy.pp0, total_energy.pp1, total_energy.platform, total_energy.dram);
                fprintf(fp, "%lf\t%lf\t%lf\t%lf\t%lf\t", total_power.package, total_power.pp0, total_power.pp1, total_power.platform, total_power.dram);
            }
#endif
#ifdef _CRAY
            struct cray_measurement total_measurements = tag->total_energy.cray_meas;
            fprintf(fp, "%lf\t%lf\t%lf\t", total_measurements.node_energy, total_measurements.cpu_energy, total_measurements.memory_energy);
            fprintf(fp, "%lf\t%lf\t%lf\t", total_measurements.node_power, total_measurements.cpu_power, total_measurements.memory_power);
            fprintf(fp, "%lf\t%lf\t%lf\t", total_measurements.node_measured_power, total_measurements.cpu_measured_power, total_measurements.memory_measured_power);
#else
#ifdef _BGQ
            struct bgq_measurement bgq_meas = tag->total_energy.bgq_meas;
            printf("%lf\n", bgq_meas.card_power);
            printf("%lf\n", bgq_meas.cpu);
            printf("%lf\n", bgq_meas.dram);
            printf("%lf\n", bgq_meas.optics);
            printf("%lf\n", bgq_meas.pci);
            printf("%lf\n", bgq_meas.network);
            printf("%lf\n", bgq_meas.link_chip);
            printf("%lf\n", bgq_meas.sram);
            write_bgq_output(&fp, &bgq_meas);
#endif
#endif
            fprintf(fp, "%d\t%d\n", tag->monitor_rank, tag->monitor_id);
        }

        fclose(fp);
    }

    return 0;
}

int pcap_tags_to_file (struct system_info_t * system_info, struct monitor_t * monitor)
{
#ifdef _MSR
    if (monitor->imonitor)
    {
        FILE *fp = open_file("PoLiMEr_powercap-tags", monitor);
        if (fp == NULL)
            return 1;

#ifndef _HEADER_OFF
        fprintf(fp, "Tag ID\tZone\tTimestamp\tPower Cap Long (W)\tPower Cap Short (W)\tTime Window Long (s)\tTime Window Short (s)\tTime since start (s)\tPCAP FLAG\tNumber of active poli tags\tPoLiMer tag list\n");
#endif

        int tag_num;
        for (tag_num = 0; tag_num < system_info->num_pcap_tags; tag_num++)
        {
            struct pcap_tag *tag = &system_info->pcap_tag_list[tag_num];
            double start_offset = tag->wtime - system_info->initial_mpi_wtime;

            char time_str_buffer[20];
            get_timestamp(start_offset, time_str_buffer, sizeof(time_str_buffer), &system_info->initial_start_time);

            fprintf(fp, "%d\t%s\t%s\t%lf\t%lf\t%lf\t%lf\t%lf\t%d\t%d\t", tag->id,
                tag->zone, time_str_buffer, tag->watts_long, tag->watts_short,
                tag->seconds_long, tag->seconds_short, start_offset,
                tag->pcap_flag, tag->num_active_poli_tags);

            int i;
            for (i = 0; i < tag->num_active_poli_tags; i++)
            {
                struct poli_tag etag = tag->active_poli_tags[i];
                if (i < tag->num_active_poli_tags - 1)
                    fprintf(fp, "%s__", etag.tag_name);
                else
                    fprintf(fp, "%s", etag.tag_name);
            }
            fprintf(fp, "\n");
        }
        fclose(fp);
    }
#endif
    return 0;
}

int polling_info_to_file (struct system_info_t * system_info, struct monitor_t * monitor, struct poller_t * poller)
{
    if (monitor->imonitor)
    {
        if (poller->time_counter <= 0)
            return 0;

#ifndef _TIMER_OFF

        FILE *fp = open_file("PoLiMEr", monitor);
        if (fp == NULL)
            return 1;

        int zone;
#ifndef _HEADER_OFF
        fprintf(fp, "Count\tTimestamp\tTime since start (s)\tPoll Time Diff (s)\t");
#ifdef _MSR
        if (!system_info->sysmsr->error_state)
        {
            fprintf(fp, "RAPL pkg E (J)\tRAPL pp0 E (J)\tRAPL pp1 E (J)\tRAPL platform E (J)\tRAPL dram E (J)\t");
            fprintf(fp, "RAPL pkg E since start (J)\tRAPL pp0 E since start (J)\tRAPL pp1 E(J) since start\tRAPL platform E (J) since start\tRAPL dram E (J) since start\t");
            fprintf(fp, "RAPL pkg P (W)\tRAPL pp0 P (W)\tRAPL pp1 P (W)\tRAPL platform P (W)\tRAPL dram P (W)");
        }
#endif
#ifdef _CRAY
        fprintf(fp, "\tCray node E (J)\tCray cpu E (J)\tCray memory E (J)\t");
        fprintf(fp, "Cray node E since start (J)\tCray cpu E since start (J)\tCray memory E since start (J)\t");
        fprintf(fp, "Cray node P (W)\tCray cpu P (W)\tCray memory P (W)\t");
        fprintf(fp, "Cray node P calc (W)\tCray cpu P calc (W)\tCray memory P calc (W)\t");
        fprintf(fp, "Cpufreq frequency (MHz)\tCray frequency (MHz)\t");
#else
#ifdef _BGQ
        write_bgq_header(&fp);
        write_bgq_ediff_header(&fp);
#endif
        fprintf(fp, "\tCpufreq frequency (MHz)\t");
#endif
#ifdef _MSR
        if (!system_info->sysmsr->error_state)
        {
            for (zone = 0; zone < system_info->sysmsr->num_zones - 1; zone++)
            {
                fprintf(fp, "%s power cap long (W)\t", get_zone_name_by_index(zone));
                fprintf(fp, "%s power cap short (W)\t", get_zone_name_by_index(zone));
            }
            fprintf(fp, "%s power cap long (W)\t", get_zone_name_by_index(system_info->sysmsr->num_zones - 1));
            fprintf(fp, "%s power cap short (W)\n", get_zone_name_by_index(system_info->sysmsr->num_zones - 1));
        }
#endif
#endif
        int counter;
        for (counter = 0; counter < poller->time_counter; counter++)
        {
            if (system_info->num_poli_tags > 0)
                get_poli_tags_for_time_counter(fp, counter, system_info, monitor);

            if (system_info->num_pcap_tags > 0)
            {
                get_pcap_tag_for_time_counter(fp, counter, system_info, monitor);
            }

            struct system_poll_info *info = &system_info->system_poll_list[counter];

            double time_from_start = info->wtime - system_info->initial_mpi_wtime;

            char time_str_buffer[20];
            get_timestamp(time_from_start, time_str_buffer, sizeof(time_str_buffer), &system_info->initial_start_time);

            fprintf(fp, "%d\t%s\t%lf\t%lf\t", info->counter, time_str_buffer, time_from_start, info->time_diff);

#ifdef _MSR
            if (!system_info->sysmsr->error_state)
            {
                struct rapl_energy *energy_j = &(info->current_energy.rapl_energy);
                struct rapl_power *watts = &(info->computed_power.rapl_power);

                //fprintf(fp, "%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t", energy_j->package, energy_j->pp0, energy_j->pp1, energy_j->platform, energy_j->dram);
                //fprintf(fp, "%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t", (energy_j->package - system_info->initial_energy.rapl_energy.package), (energy_j->pp0 - system_info->initial_energy.rapl_energy.pp0), (energy_j->pp1 - system_info->initial_energy.rapl_energy.pp1), (energy_j->platform - system_info->initial_energy.rapl_energy.platform), (energy_j->dram - system_info->initial_energy.rapl_energy.dram));
                fprintf(fp, "%lf\t%lf\t%lf\t%lf\t%lf\t", energy_j->package, energy_j->pp0, energy_j->pp1, energy_j->platform, energy_j->dram);
                fprintf(fp, "%lf\t%lf\t%lf\t%lf\t%lf\t", (energy_j->package - system_info->initial_energy.rapl_energy.package), (energy_j->pp0 - system_info->initial_energy.rapl_energy.pp0), (energy_j->pp1 - system_info->initial_energy.rapl_energy.pp1), (energy_j->platform - system_info->initial_energy.rapl_energy.platform), (energy_j->dram - system_info->initial_energy.rapl_energy.dram));
                fprintf(fp, "%lf\t%lf\t%lf\t%lf\t%lf\t", watts->package, watts->pp0, watts->pp1, watts->platform, watts->dram);
            }
#endif
#ifdef _CRAY
            struct cray_measurement *cmeasurement = &(info->current_energy.cray_meas);
            struct cray_measurement *cpower = &(info->computed_power.cray_meas);

            fprintf(fp, "%lf\t%lf\t%lf\t", cmeasurement->node_energy, cmeasurement->cpu_energy, cmeasurement->memory_energy);
            fprintf(fp, "%lf\t%lf\t%lf\t", (cmeasurement->node_energy - system_info->initial_energy.cray_meas.node_energy), (cmeasurement->cpu_energy - system_info->initial_energy.cray_meas.cpu_energy), (cmeasurement->memory_energy - system_info->initial_energy.cray_meas.memory_energy));
            fprintf(fp, "%lf\t%lf\t%lf\t", cmeasurement->node_power, cmeasurement->cpu_power, cmeasurement->memory_power);
            fprintf(fp, "%lf\t%lf\t%lf\t", cpower->node_measured_power, cpower->cpu_measured_power, cpower->memory_measured_power);
            fprintf(fp, "%lf\t%lf\t", info->freq.freq, info->freq.cray_freq);
#else
#ifdef _BGQ
            struct bgq_measurement *bgq_meas = &(info->current_energy.bgq_meas);
            write_bgq_output(&fp, bgq_meas);
            write_bgq_ediff(&fp, bgq_meas, &(system_info->initial_energy.bgq_meas));
#endif
            fprintf(fp, "%lf\t", info->freq.freq);
#endif
            for (zone = 0; zone < system_info->sysmsr->num_zones - 1; zone++)
            {
                fprintf(fp, "%lf\t", info->pcap_info_list[zone].watts_long);
                fprintf(fp, "%lf\t", info->pcap_info_list[zone].watts_short);
            }
            fprintf(fp, "%lf\t", info->pcap_info_list[system_info->sysmsr->num_zones - 1].watts_long);
            fprintf(fp, "%lf\n", info->pcap_info_list[system_info->sysmsr->num_zones - 1].watts_short);
        }
        fclose(fp);
#else //_TIMER_OFF is set
        return 0;
#endif
    }

    return 0;
}