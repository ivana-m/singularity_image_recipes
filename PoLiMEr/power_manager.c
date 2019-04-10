#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mpi.h>
#include <math.h>

#include "PoLiLog.h"
//#include "PoLiMEr.h"
#include "power_manager.h"
#include "helpers.h"
#include "output.h"

struct power_manager_t *power_manager = 0;
MPI_Comm SA_allcomm;
int SA_allsize;
double *last_sync_runtimes;
double *current_runtimes;

static void get_past_time_and_power (struct system_info_t * system_info, double * time, double * power);
static void get_past_power_from_poller(struct system_info_t * system_info, double *time, double *power, int use_average, int use_median, int use_max);
static double torben(double m[], int n);
static double median (int n, double x[], int * position);
static void median_energy (int n, uint64_t x[], int * position);
static void record_allocated_time (struct system_info_t * system_info, struct monitor_t * monitor);
static void max_power_policy (struct system_info_t * system_info, double * time, double * power);
static void median_policy (struct system_info_t * system_info, double * time, double * power);
static void max_energy_policy (struct system_info_t * system_info, double * time, double * power);
static void median_energy_policy (struct system_info_t * system_info, double * time, double * power);
static void median_energy_from_poll_averages_policy (struct system_info_t * system_info, double * time, double * power);
static void last_sync_poller_average (struct system_info_t * system_info, double * time, double * power);
static void last_sync_poller_median (struct system_info_t * system_info, double * time, double * power);
static void last_sync_poller_max (struct system_info_t * system_info, double * time, double * power);
static void average_sync_measurement_policy (double * time, double * power);
static void default_policy (struct system_info_t * system_info, double * time, double * power);
static void set_pm_algorithm (int SeeSAw, int SLURMLike, int GEOPMLike);

void init_power_manager (struct system_info_t * system_info, struct monitor_t * monitor, struct polimer_config_t * poli_config)
{
    power_manager = malloc(sizeof(struct power_manager_t));
    last_sync_runtimes = (double *) malloc(sizeof(double) * monitor->node_size);
    current_runtimes = (double *) malloc(sizeof(double) * monitor->node_size);
    power_manager->count = 0;
    power_manager->last_sync = 0;
    power_manager->sync_begin = 0;
    power_manager->last_sync_time = get_time();
    power_manager->current_time = power_manager->last_sync_time;
    power_manager->new_power_node = 0.0;
    power_manager->global_pcap = 0;
    power_manager->freq = poli_config->palloc_freq;
    power_manager->policy = poli_config->policy;
    power_manager->use_sync_window = poli_config->use_sync_window;
    power_manager->off = 0;
    power_manager->time_sum = 0;
    power_manager->log_count = 0;
    power_manager->timestep = 0;
    power_manager->measure_sync_end = poli_config->measure_sync_end;
    power_manager->simulate = poli_config->simulate_pm;
    power_manager->pm_algorithm = poli_config->pm_algorithm;
    power_manager->delta = poli_config->gp_delta;
    if (!power_manager->delta)
        power_manager->delta = START_DELTA;
    power_manager->target_met = 0;
    set_pm_algorithm(1, 0, 0);
    last_sync_runtimes[monitor->node_rank] = power_manager->last_sync_time;
    if (monitor->imonitor)
    {
        power_manager->logs = calloc(MAX_PM_LOGS, sizeof(struct pm_log_t));
        power_manager->last_sync_time = system_info->initial_mpi_wtime;
        struct power_manager_t *palloc_entry = &system_info->palloc_list[0];
        palloc_entry->count = power_manager->count;
        palloc_entry->my_last_sync_time = system_info->initial_mpi_wtime;
        palloc_entry->last_sync_time = power_manager->last_sync_time;
        palloc_entry->last_energy = system_info->initial_energy;
        palloc_entry->last_sync = 0;

        palloc_entry->node = monitor->color;
        palloc_entry->rank = monitor->world_rank;

        palloc_entry->pcap = power_manager->new_power_node;
    }
}

static void median_policy (struct system_info_t * system_info, double * time, double * power)
{
    double times[power_manager->freq], powers[power_manager->freq];
    int count = 0;
    int i;
    for (i = power_manager->count - (power_manager->freq) + 1; i <= power_manager->count; i++)
    {
        struct power_manager_t *palloc_entry = &system_info->palloc_list[i];
        double total_time = (palloc_entry->current_time - palloc_entry->last_sync_time);
        if (total_time)
        {
            times[count] = total_time;
            powers[count] = palloc_entry->total_power.rapl_power.package;
            count++;
        }
    }

    int position;
    *time = median(count, times, &position); //torben(times, count);
    *power = powers[position];
}

static void median_energy_policy (struct system_info_t * system_info, double * time, double * power)
{
    double energy_list[power_manager->freq];
    double times[power_manager->freq], powers[power_manager->freq];
    int count = 0;
    int i;
    for (i = power_manager->count - (power_manager->freq + 1); i <= power_manager->count; i++)
    {
        struct power_manager_t *palloc_entry = &system_info->palloc_list[i];
        double total_energy = palloc_entry->total_energy.rapl_energy.package;
        if (total_energy)
        {
            energy_list[count] = total_energy;
            times[count] = (palloc_entry->current_time - palloc_entry->last_sync_time);
            powers[count] = palloc_entry->total_power.rapl_power.package;
            count++;
        }
    }

    int position;
    median(count, energy_list, &position); //torben(times, count);
    *time = times[position];
    *power = powers[position];
}

static void median_energy_from_poll_averages_policy (struct system_info_t * system_info, double * time, double * power)
{
    double energy_list[power_manager->freq];
    double times[power_manager->freq], powers[power_manager->freq];
    int count = 0;
    int i;
    for (i = power_manager->count - (power_manager->freq + 1); i <= power_manager->count; i++)
    {
        struct power_manager_t *palloc_entry = &system_info->palloc_list[i];
        if (palloc_entry->average_poll_energy)
        {
            energy_list[count] = palloc_entry->average_poll_energy;
            times[count] = (palloc_entry->current_time - palloc_entry->last_sync_time);
            powers[count] = palloc_entry->average_poll_power;
            count++;
        }
    }

    int position;
    median(count, energy_list, &position); //torben(times, count);
    *time = times[position];
    *power = powers[position];
}

static void max_power_policy (struct system_info_t * system_info, double * time, double * power)
{
    struct power_manager_t *first = &system_info->palloc_list[power_manager->count - power_manager->freq];
    struct power_manager_t *second = &system_info->palloc_list[power_manager->count - 1];
    double max_power = first->total_power.rapl_power.package;
    if (second->total_power.rapl_power.package > max_power)
    {
        *time = second->current_time - second->last_sync_time;
        *power = second->total_power.rapl_power.package;
    }
    else
    {   
        *time = first->current_time - first->last_sync_time;
        *power = first->total_power.rapl_power.package;
    }
}

static void max_energy_policy (struct system_info_t * system_info, double * time, double * power)
{
    struct power_manager_t *first = &system_info->palloc_list[power_manager->count - power_manager->freq];
    struct power_manager_t *second = &system_info->palloc_list[power_manager->count - 1];
    double max_energy = first->total_energy.rapl_energy.package;
    if (second->total_energy.rapl_energy.package > max_energy)
    {
        *time = second->current_time - second->last_sync_time;
        *power = second->total_power.rapl_power.package;
    }
    else
    {   
        *time = first->current_time - first->last_sync_time;
        *power = first->total_power.rapl_power.package;
    }
}

static void last_sync_poller_average (struct system_info_t * system_info, double * time, double * power)
{
    struct power_manager_t *palloc_entry = &system_info->palloc_list[power_manager->count];
    *time = palloc_entry->current_time - palloc_entry->last_sync_time;
    *power = palloc_entry->average_poll_power;
}

static void last_sync_poller_median (struct system_info_t * system_info, double * time, double * power)
{
    struct power_manager_t *palloc_entry = &system_info->palloc_list[power_manager->count];
    *time = palloc_entry->current_time - palloc_entry->last_sync_time;
    *power = palloc_entry->median_poll_power;
}

static void last_sync_poller_max (struct system_info_t * system_info, double * time, double * power)
{
    struct power_manager_t *palloc_entry = &system_info->palloc_list[power_manager->count];
    *time = palloc_entry->current_time - palloc_entry->last_sync_time;
    *power = palloc_entry->max_poll_power;
}

static void last_sync_poller_total (struct system_info_t * system_info, double * time, double * power)
{
    struct power_manager_t *palloc_entry = &system_info->palloc_list[power_manager->count];
    *time = palloc_entry->current_time - palloc_entry->last_sync_time;
    *power = palloc_entry->total_poll_power.package;
}


static void last_sync_power (struct system_info_t * system_info, double * time, double * power)
{
    struct power_manager_t *palloc_entry = &system_info->palloc_list[power_manager->count];
    *time = palloc_entry->current_time - palloc_entry->last_sync_time;
    *power = palloc_entry->total_power.rapl_power.package;
}


static void average_sync_measurement_policy (double * time, double * power)
{
    *time = power_manager->time_sum / (double) power_manager->freq;
    *power = power_manager->power_sum / (double) power_manager->freq;
}

static void get_past_power_from_poller(struct system_info_t * system_info, double *time, double *power, int use_average, int use_median, int use_max)
{
    struct power_manager_t *palloc_entry = &system_info->palloc_list[power_manager->count];
    *time = palloc_entry->current_time - palloc_entry->last_sync_time;

    int num_poller_entries = power_manager->sync_begin - power_manager->last_sync + 1;
    if (num_poller_entries == 0)
    {
        *power = 0.0;
        return;
    }
    double max_power = 0.0;
    double power_sum = 0.0;
    double powers[num_poller_entries];
    int i;
    int count = 0;
    for (i = power_manager->last_sync; i <= power_manager->sync_begin; i++)
    {
        double current_power = system_info->system_poll_list[i].computed_power.rapl_power.package;
        if (current_power > 0.3 * system_info->power_info.package_minimum_power && current_power < 1.3 * system_info->power_info.package_maximum_power) //over and close to 300 is unrealistic so skip it
        {
            if (current_power >= max_power)
                max_power = current_power;
            power_sum += current_power;
            powers[count] = current_power;
            count++;
        }
        //else
        //    num_poller_entries--;

        //if (num_poller_entries > 0)
        //    max_power = power_sum / (double) (num_poller_entries - (start - power_manager->last_sync));
    }
    if (use_average)
        *power = power_sum / (double) count;
    else if (use_median)
    {
        int position;
        *power = median(count, powers, &position);
    }
    else if (use_max)
        *power = max_power;
    else
        *power = power_sum / (double) count;
        
}

static void default_policy (struct system_info_t * system_info, double * time, double * power)
{
    last_sync_power(system_info, time, power);
}

static void get_past_time_and_power (struct system_info_t * system_info, double * time, double * power)
{

    if (!power_manager->use_sync_window)
        default_policy(system_info, time, power);
    else if (strcmp(power_manager->policy, "LAST_SYNC_POLLER_TOTAL") == 0)
        last_sync_poller_total(system_info, time, power);
    else if (strcmp(power_manager->policy, "LAST_SYNC_POLLER_AVERAGE") == 0 )
        last_sync_poller_average(system_info, time, power);
    else if (strcmp(power_manager->policy, "LAST_SYNC_POLLER_MEDIAN") == 0 )
        last_sync_poller_median(system_info, time, power);
    else if (strcmp(power_manager->policy, "LAST_SYNC_POLLER_MAX") == 0 )
        last_sync_poller_max(system_info, time, power);
    else if (strcmp(power_manager->policy, "LAST_SYNC_POWER") == 0 )
        last_sync_power(system_info, time, power);
    else if (strcmp(power_manager->policy, "AVERAGE_SYNC_MEASUREMENTS") == 0)
        average_sync_measurement_policy(time, power);
    else if (strcmp(power_manager->policy, "AVERAGE_POLL_POWER") == 0)
        get_past_power_from_poller(system_info, time, power, 1, 0, 0);
    else if (strcmp(power_manager->policy, "MAX_POLL_POWER") == 0)
        get_past_power_from_poller(system_info, time, power, 0, 0, 1);
    else if (strcmp(power_manager->policy, "MEDIAN_POLL_POWER") == 0)
        get_past_power_from_poller(system_info, time, power, 0, 1, 0);
    else if (strcmp(power_manager->policy, "MAX_ENERGY") == 0)
        max_energy_policy(system_info, time, power);
    else if (strcmp(power_manager->policy, "MEDIAN_ENERGY") == 0)
        median_energy_policy(system_info, time, power);
    else if (strcmp(power_manager->policy, "MEDIAN_ENERGY_POLL") == 0)
        median_energy_from_poll_averages_policy(system_info, time, power);
    else if (strcmp(power_manager->policy, "MEDIAN") == 0)
        median_policy(system_info, time, power);
    else
        default_policy(system_info, time, power);
    
}

static void set_pm_algorithm(int SeeSAw, int SLURMLike, int GEOPMLike)
{
    power_manager->SeeSAw = SeeSAw;
    power_manager->SLURMLike = SLURMLike;
    power_manager->GEOPMLike = GEOPMLike;

    if ( !power_manager->SeeSAw && !power_manager->SLURMLike && !power_manager->GEOPMLike)
        power_manager->SeeSAw = 1;
}

int allocate_power(double power_cap, struct system_info_t * system_info, struct monitor_t * monitor, struct poller_t * poller)
{
    if (strcmp(power_manager->pm_algorithm, "SeeSAw") == 0)
    {
        set_pm_algorithm(1, 0, 0);
        return SeeSAw(power_cap, system_info, monitor, poller);
    }
    if (strcmp(power_manager->pm_algorithm, "SLURMLike") == 0)
    {
        set_pm_algorithm(0, 1, 0);
        return SLURMLike(power_cap, system_info, monitor, poller);
    }
    if (strcmp(power_manager->pm_algorithm, "GEOPMLike") == 0)
    {
        set_pm_algorithm(0, 0, 1);
        return GEOPMLike(power_cap, system_info, monitor, poller);
    }
}

int SeeSAw(double power_cap, struct system_info_t * system_info, struct monitor_t * monitor, struct poller_t * poller)
{
    int ret = 0;
    int allocate_power = 0;

    if (monitor->imonitor)
        poli_log(DEBUG, monitor, "Entered %s\n", __FUNCTION__);

    //t2
    power_manager->pcap = power_cap;
    if (power_manager->use_sync_window || power_manager->count == 0)
        sync_start_measurements(system_info, monitor, poller);

    if (power_manager->count % power_manager->freq == 0 && power_manager->count > 0)
    {
        allocate_power = 1;
        if (!power_manager->use_sync_window)
            sync_start_measurements(system_info, monitor, poller);
    }

    if (monitor->imonitor && allocate_power && !power_manager->off)
    {
        double max_power = 0;
        double start_time = power_manager->last_sync_time;
        double end_time = power_manager->current_time;

        double max_power_sa_nodes, power_cap_all_nodes;

        double measured_time, average_sync_power;
        get_past_time_and_power(system_info, &measured_time, &average_sync_power);
        max_power = average_sync_power;

        if (!max_power)
            max_power = power_cap;

        /* The S&A master ranks need to know what the total power is from their respective partitions*/
        MPI_Allreduce(&max_power, &max_power_sa_nodes, 1, MPI_DOUBLE, MPI_SUM, monitor->sa_comm);


        /*Calculate the optimal powers*/
        double my_alpha, other_alpha, my_poweropt, other_poweropt;


        my_alpha = 1.0 / (double) (max_power_sa_nodes * measured_time);


        power_cap_all_nodes = power_manager->global_pcap;

        if (!power_cap_all_nodes) //if global power cap was not set through app, compute it
        {
            MPI_Allreduce(&power_cap, &power_cap_all_nodes, 1, MPI_DOUBLE, MPI_SUM, monitor->monitors_comm);
            power_manager->global_pcap = power_cap_all_nodes;
        }
        
        double alpha_sum;

        MPI_Allreduce(&my_alpha, &alpha_sum, 1, MPI_DOUBLE, MPI_SUM, monitor->monitors_comm);

        int other_size = monitor->monitors_size - monitor->sa_size;
        double my_total_alpha = my_alpha * monitor->sa_size;
        other_alpha = (alpha_sum - my_total_alpha) / (double) other_size;

        my_poweropt = (other_alpha * power_cap_all_nodes) / (my_alpha + other_alpha);

        double previous_power = max_power_sa_nodes;

        if (power_manager->opt_power_node)
            previous_power = power_manager->opt_power_node; // max_power_sa_nodes; 

        //previous_power = power_cap * monitor->sa_size;

        double ratio = my_poweropt / power_cap_all_nodes;
        double other_ratio = 1 - ratio;

        //if (ratio > 0.5)
        //    ratio = other_ratio;

        other_poweropt = power_cap_all_nodes - my_poweropt;

        double lowlimit = system_info->power_info.package_minimum_power;
        double original_poweropt = my_poweropt;

        if (my_poweropt / monitor->sa_size < lowlimit)
            my_poweropt = lowlimit * monitor->sa_size;
        if (other_poweropt / other_size < lowlimit)
            my_poweropt = power_cap_all_nodes - lowlimit * other_size;

        if (power_manager->count == 0 && previous_power == 0)
            previous_power = power_cap * monitor->sa_size; //my_poweropt;

        power_manager->opt_power_node = my_poweropt;

        double allocated_power;

        allocated_power = (1 - ratio) * previous_power + ratio * my_poweropt;

        power_manager->new_power_node = allocated_power / monitor->sa_size;

        poli_log(DEBUG, monitor, "ID=%d, Tstep: %d, alloc = %f, r = %f, per node: %f, prev: %f, My Pow: %f, Other pow: %f, max_pow: %f, cap all nodes: %f, start: %f, end: %f, avg time: %f, avg power sum: %f\n",
            system_info->palloc_count, power_manager->timestep, 
            allocated_power, ratio, 
            power_manager->new_power_node, previous_power, 
            my_poweropt, other_poweropt, max_power, power_cap_all_nodes, 
            start_time - system_info->initial_mpi_wtime, 
            end_time - system_info->initial_mpi_wtime, 
            measured_time, max_power_sa_nodes);

        struct pm_log_t *log = &power_manager->logs[power_manager->log_count];
        log->palloc_count = system_info->palloc_count;
        log->pm_count = power_manager->count;
        log->my_alpha = my_alpha;
        log->other_alpha = other_alpha;
        log->world_rank = monitor->world_rank;
        log->timestep = power_manager->timestep;
        log->allocated_power = allocated_power;
        log->ratio = ratio;
        log->new_power_per_node = power_manager->new_power_node;
        log->previous_total_power = previous_power;
        log->my_opt_power = original_poweropt;
        log->adjusted_opt_power = my_poweropt;
        log->other_opt_power = other_poweropt;
        log->max_observed_power = max_power;
        log->pcap_all_nodes = power_cap_all_nodes;
        log->start_time = start_time - system_info->initial_mpi_wtime;
        log->end_time = end_time - system_info->initial_mpi_wtime;
        log->average_time = measured_time;
        log->observed_power_all_nodes = max_power_sa_nodes;
        power_manager->log_count++;
    }

    if (monitor->imonitor)
        poli_log(DEBUG, monitor, "Finished %s\n", __FUNCTION__);

    return ret;
}


int SLURMLike (double power_cap, struct system_info_t * system_info, struct monitor_t * monitor, struct poller_t * poller)
{
    int ret = 0;
    int allocate_power = 0;

    if (monitor->imonitor)
        poli_log(DEBUG, monitor, "Entered %s\n", __FUNCTION__);

    //t2
    power_manager->pcap = power_cap;
    if (power_manager->use_sync_window || power_manager->count == 0)
    {
        sync_start_measurements(system_info, monitor, poller);
        //power_manager->new_power_node = power_cap;
    }

    if (power_manager->count % power_manager->freq == 0 && power_manager->count > 0)
    {
        allocate_power = 1;
        if (!power_manager->use_sync_window)
            sync_start_measurements(system_info, monitor, poller);
    }

    if (monitor->imonitor && allocate_power && !power_manager->off)
    {   
        if (!power_manager->global_pcap)
            MPI_Allreduce(&power_cap, &power_manager->global_pcap, 1, MPI_DOUBLE, MPI_SUM, monitor->monitors_comm);

        /*commented out to test uneven iniitial power assignemnt - power will be divided evenly if user sets the same power cap to S and A*/
        if (!power_manager->new_power_node)
            power_manager->new_power_node = power_manager->global_pcap / monitor->num_monitors;

        double my_power_cap = power_manager->new_power_node; //previously set power cap

        double sync_time, sync_power;
        double slack_power = 0;
        double total_slack_power = 0;
        double total_requested = 0;
        get_past_time_and_power(system_info, &sync_time, &sync_power);
        double total_power_consumed = 0;
        MPI_Allreduce(&sync_power, &total_power_consumed, 1, MPI_DOUBLE, MPI_SUM, monitor->monitors_comm);

        double send_my_watts = 0;
        double request_watts = 0;
        int at_pcap = 0;
        int at_thresh = 0;
        int num_at_pcap = 0;
        int num_at_thresh = 0;
        if (sync_power < my_power_cap * SLURM_LOWER_THRESH)
        {
            double w1 = (my_power_cap - sync_power) / 2.0;
            double w2 = (system_info->power_info.package_maximum_power - system_info->power_info.package_minimum_power ) * SLURM_DECREASE_RATE;
            if (w1 < w2)
                power_manager->new_power_node = my_power_cap - w1;
            else
                power_manager->new_power_node = my_power_cap - w2;

            if (power_manager->new_power_node < system_info->power_info.package_minimum_power)
                power_manager->new_power_node = system_info->power_info.package_minimum_power ;

            send_my_watts = power_manager->new_power_node;
        }
        else if (sync_power < my_power_cap && sync_power >= SLURM_UPPER_THRESH * my_power_cap && sync_power <= total_power_consumed / monitor->num_monitors)
          {
            power_manager->new_power_node = my_power_cap + ((system_info->power_info.package_maximum_power - system_info->power_info.package_minimum_power) * SLURM_INCREASE_RATE);
            request_watts = power_manager->new_power_node;
            at_thresh = 1;
        }
        else
            at_pcap = 1;

        int len = 4;
        double receive_buff[len];
        double send_buff[4] = {send_my_watts, (double) at_pcap, request_watts, (double) at_thresh};

        MPI_Allreduce(&send_buff, &receive_buff, len, MPI_DOUBLE, MPI_SUM, monitor->monitors_comm);
        total_slack_power = receive_buff[0];
        num_at_pcap = (int) receive_buff[1];
        total_requested = receive_buff[2];
        num_at_thresh = (int) receive_buff[3];
        double delta = total_slack_power;

        if (total_requested && total_slack_power + total_requested <= power_manager->global_pcap)
            delta = total_slack_power + total_requested;
        else if (at_thresh)
            power_manager->new_power_node = my_power_cap;

        if (num_at_pcap)
        {
            if (at_pcap)
            {
                if (delta <= power_manager->global_pcap)
                    power_manager->new_power_node = (power_manager->global_pcap - delta) / num_at_pcap;
            }

            if (((power_manager->global_pcap - delta) / num_at_pcap) < system_info->power_info.package_minimum_power )
            {
                if (at_pcap)
                    power_manager->new_power_node = system_info->power_info.package_minimum_power;
                else
                    power_manager->new_power_node = (power_manager->global_pcap - (system_info->power_info.package_minimum_power * num_at_pcap)) / (monitor->num_monitors - num_at_pcap);
            }
        }
        else //all nodes run below power cap
        {
            if (!at_thresh)
                power_manager->new_power_node = my_power_cap;
        }

        struct pm_log_t *log = &power_manager->logs[power_manager->log_count];
        log->palloc_count = system_info->palloc_count;
        log->pm_count = power_manager->count;
        log->world_rank = monitor->world_rank;
        log->timestep = power_manager->timestep;
        log->new_power_per_node = power_manager->new_power_node;
        log->average_time = sync_time;
        log->slack_power = total_slack_power;
        log->at_pcap = at_pcap;
        log->max_observed_power = sync_power;
        log->pcap_all_nodes = my_power_cap;
        power_manager->log_count++;
    }

    if (monitor->imonitor)
        poli_log(DEBUG, monitor, "Finished %s\n", __FUNCTION__);

    return ret;
}


int GEOPMLike (double power_cap, struct system_info_t * system_info, struct monitor_t * monitor, struct poller_t * poller)
{
    if (monitor->imonitor)
        poli_log(DEBUG, monitor, "Entered %s\n", __FUNCTION__);

    int ret = 0;
    int allocate_power = 0;
    double receive_buff[monitor->node_size];
    double send_buff[monitor->node_size];

    double median_node_runtime, longest_runtime;
    int median_runtime_rank;
    
    //t2
    sync_start_measurements(system_info, monitor, poller);
    power_manager->pcap = power_cap;
    // if (power_manager->count == 0)
    // {
    //     power_manager->new_power_node = power_cap;
    // }
    //else
    if (power_manager->count)
    {
        allocate_power = 1;
        int i;
        for (i = 0; i < monitor->node_size; i++)
            send_buff[i] = 0.0;
        send_buff[monitor->node_rank] = current_runtimes[monitor->node_rank] - last_sync_runtimes[monitor->node_rank];
        MPI_Allreduce(&send_buff, &receive_buff, monitor->node_size, MPI_DOUBLE, MPI_SUM, monitor->mynode_comm);
        median_node_runtime = median(monitor->node_size, receive_buff, &median_runtime_rank);
        MPI_Allreduce(&median_node_runtime, &longest_runtime, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
    }

    if (monitor->imonitor && allocate_power && !power_manager->off)
    {
        poli_log(DEBUG, monitor, "Delta: %lf\n", power_manager->delta);
        if (!power_manager->new_power_node)
        {
            power_manager->new_power_node = power_manager->global_pcap / monitor->num_monitors;
            power_manager->pcap = power_manager->new_power_node;
        }

        double my_power_cap = power_manager->pcap;

        double target_runtime = longest_runtime * (1 - RUNTIME_THRESH);
        if (median_node_runtime > target_runtime)
            power_manager->target_met = 1;
        else
            power_manager->target_met = 0;

        double sync_time, sync_power;
        double slack_power = 0;
        double total_slack_power = 0;
        double lowlimit = system_info->power_info.package_minimum_power;
        int at_min_pow = 0;

        last_sync_power(system_info, &sync_time, &sync_power);

        if (power_manager->new_power_node <= lowlimit)
            power_manager->target_met = 1;

        if (!power_manager->target_met)
        {
            if (median_node_runtime <= target_runtime)
                power_manager->new_power_node = power_manager->new_power_node - power_manager->delta;
            else
            {
                if (power_manager->new_power_node < my_power_cap)
                {
                    power_manager->new_power_node += power_manager->delta;
                    if (power_manager->new_power_node > my_power_cap)
                        power_manager->new_power_node = my_power_cap;
                }
                power_manager->target_met = 1;
            }
        }

        if (power_manager->new_power_node == my_power_cap)
        {
            power_manager->delta = power_manager->delta / 2.0;
            if (power_manager->delta < MIN_DELTA)
                power_manager->delta = MIN_DELTA;
        }

        if (power_manager->new_power_node <= lowlimit)
        {
            power_manager->new_power_node = lowlimit;
            at_min_pow = 1;
        }

        double rb[2];
        double sb[2] = {power_manager->new_power_node, at_min_pow};

        MPI_Allreduce(&sb, &rb, 2, MPI_DOUBLE, MPI_SUM, monitor->monitors_comm);

        double total_power = rb[0];
        int num_atminpow = rb[1];
        double extra_power = 0;

        if (total_power <= power_manager->global_pcap)
        {
            total_slack_power = power_manager->global_pcap - total_power;
            extra_power = total_slack_power / (monitor->num_monitors);

            power_manager->new_power_node += extra_power;
        }

        // if (power_manager->new_power_node > power_cap)
        //     power_manager->new_power_node = power_cap;

        struct pm_log_t *log = &power_manager->logs[power_manager->log_count];
        log->palloc_count = system_info->palloc_count;
        log->pm_count = power_manager->count;
        log->world_rank = monitor->world_rank;
        log->timestep = power_manager->timestep;
        log->new_power_per_node = power_manager->new_power_node;
        log->average_time = sync_time;
        log->slack_power = total_slack_power;
        log->extra_power = extra_power;
        log->median_node_runtime = median_node_runtime;
        log->median_runtime_rank = median_runtime_rank;
        log->target_runtime = target_runtime;
        log->delta = power_manager->delta;
        log->max_observed_power = sync_power;
        log->target_met = power_manager->target_met;
        power_manager->log_count++;
    }

    if (monitor->imonitor)
        poli_log(DEBUG, monitor, "Finished %s\n", __FUNCTION__);

    return ret;
}




void set_global_power_cap (double pcap)
{
    power_manager->global_pcap = pcap;
}


int allocate_power_min_collectives (double power_cap, struct system_info_t * system_info, struct monitor_t * monitor, struct poller_t * poller)
{
    int ret = 0;

    if (!monitor->imonitor)
        return ret;

    //stop_timer();

    if ((power_manager->count > 0 && power_manager->count % power_manager->freq != 0) || power_manager->count == 0)
        return ret;

    int last_before_sync = poller->time_counter;
    power_manager->current_time = get_time();
    power_manager->current_energy = read_current_energy(system_info);

    //poli_set_power_cap(power_cap); //want to make sure the sync period is also under power cap

    double max_power = 0;
    double start_time = 0;
    double end_time = 0;

    if (power_manager->last_sync == -1)
        power_manager->last_sync = 0;

    int source = 0;
    int destination = 1;

    start_time = power_manager->last_sync_time;
    end_time = power_manager->current_time;

    double max_time_all_nodes, min_time_all_nodes, max_power_sa_nodes, power_cap_all_nodes;

    /* Obtain start and end time*/
    MPI_Allreduce(&end_time, &max_time_all_nodes, 1, MPI_DOUBLE, MPI_MAX, monitor->sa_comm);
    MPI_Allreduce(&start_time, &min_time_all_nodes, 1, MPI_DOUBLE, MPI_MIN, monitor->sa_comm);

    start_time = min_time_all_nodes;
    end_time = max_time_all_nodes;

    if (last_before_sync > power_manager->last_sync) //If enough time has passed use the poller information
    {
        int time;
        for (time = power_manager->last_sync; time < last_before_sync; time++)
        {
            double current_power = system_info->system_poll_list[time].computed_power.rapl_power.package;
            if (current_power >= max_power)
                max_power = current_power;
        }
    }
    else if (last_before_sync == power_manager->last_sync) //just get power between start and end of last non-sync period
    {
        rapl_compute_total_energy(&(power_manager->total_energy.rapl_energy), &(power_manager->current_energy.rapl_energy), &(power_manager->last_energy.rapl_energy));
        rapl_compute_total_power(&(power_manager->total_power.rapl_power), &(power_manager->total_energy.rapl_energy), end_time - start_time);
        max_power = power_manager->total_power.rapl_power.package;
    }

    /* The S&A master ranks need to know what the total power is from their respective partitions*/
    MPI_Reduce(&max_power, &max_power_sa_nodes, 1, MPI_DOUBLE, MPI_SUM, 0, monitor->sa_comm);

    /*Calculate the optimal powers*/
    double my_alpha, other_alpha, my_poweropt, other_poweropt;

    if (monitor->sa_master)
        my_alpha = 1.0 / (double) (max_power_sa_nodes * (max_time_all_nodes - min_time_all_nodes));
    else
        my_alpha = 0.0;

    power_cap_all_nodes = power_manager->global_pcap;

    if (!power_cap_all_nodes) //if global power cap was not set through app, compute it
    {
        MPI_Allreduce(&power_cap, &power_cap_all_nodes, 1, MPI_DOUBLE, MPI_SUM, monitor->monitors_comm);
        power_manager->global_pcap = power_cap_all_nodes;
    }


    int my_tag = 4 * power_manager->count;
    int other_tag = 4 * power_manager->count + 1;

    //MPI_Request reqs[2]; //sendreq, recvreq;
    //MPI_Status status;

     if (monitor->sa_master)
     {
         //exchange alphas between the master ranks
         if (monitor->sa_master_rank == source)
         {
    //         //MPI_Isend(&my_alpha, 1, MPI_DOUBLE, destination, my_tag, monitor->sa_master_comm, &reqs[0]);
    //         //MPI_Irecv(&other_alpha, 1, MPI_DOUBLE, destination, other_tag, monitor->sa_master_comm, &reqs[1]);
            MPI_Send(&my_alpha, 1, MPI_DOUBLE, destination, my_tag, monitor->sa_master_comm);
            MPI_Recv(&other_alpha, 1, MPI_DOUBLE, destination, other_tag, monitor->sa_master_comm, MPI_STATUS_IGNORE);
         }
         else if (monitor->sa_master_rank == destination)
         {
    //         //MPI_Isend(&my_alpha, 1, MPI_DOUBLE, source, other_tag, monitor->sa_master_comm, &reqs[0]);
    //         //MPI_Irecv(&other_alpha, 1, MPI_DOUBLE, source, my_tag, monitor->sa_master_comm, &reqs[1]);
            MPI_Send(&my_alpha, 1, MPI_DOUBLE, source, other_tag, monitor->sa_master_comm);
            MPI_Recv(&other_alpha, 1, MPI_DOUBLE, source, my_tag, monitor->sa_master_comm, MPI_STATUS_IGNORE);

         }

    //     //MPI_Waitall(2, reqs, MPI_STATUS_IGNORE);

         my_poweropt = (other_alpha * power_cap_all_nodes) / (my_alpha + other_alpha);

    //     // //echange optimal powers between S&A to check if we violate power later on

    //     // if (monitor->sa_master_rank == source)
    //     // {
    //     //     MPI_Send(&my_poweropt, 1, MPI_DOUBLE, destination, 0, monitor->sa_master_comm);
    //     //     MPI_Recv(&other_poweropt, 1, MPI_DOUBLE, destination, 1, monitor->sa_master_comm, MPI_STATUS_IGNORE);
    //     // }
    //     // else if (monitor->sa_master_rank == destination)
    //     // {
    //     //     MPI_Send(&my_poweropt, 1, MPI_DOUBLE, source, 1, monitor->sa_master_comm);
    //     //     MPI_Recv(&other_poweropt, 1, MPI_DOUBLE, source, 0, monitor->sa_master_comm, MPI_STATUS_IGNORE);
    //     // }

     }

    double previous_power;
    /*
    if (power_manager->count == 0)
        previous_power = max_power_sa_nodes;
    else
        previous_power = power_manager->new_power_node;
    */
    previous_power = max_power_sa_nodes;

    //previous_power = power cap //this was terrible for small case

    power_manager->opt_power_node = my_poweropt;

    double ratio = my_poweropt / power_cap_all_nodes;

    double allocated_power = (1 - ratio) * previous_power + ratio * my_poweropt;

    MPI_Bcast(&allocated_power, 1, MPI_DOUBLE, 0, monitor->sa_comm);

    power_manager->new_power_node = allocated_power / monitor->sa_size;

    // double shift_extra = 0.0;
    // double shifted = 0;
    // double min_thresh = 75;

    // if (power_manager->new_power_node < min_thresh)
    // {
    //     power_manager->new_power_node = min_thresh;
    //     shifted = 1;
    //     allocated_power = power_manager->new_power_node * monitor->sa_size;
    // }

    // double send_array[2] = {0.0,0.0};
    // double recv_array[2];

    // if (monitor->sa_master)
    // {
    //     send_array[0] = shifted;
    //     send_array[1] = allocated_power;
    // }


    // if (monitor->sa_master)
    //     printf("count: %d, Extra shifting\n", power_manager->count);

    // MPI_Allreduce(&send_array, &recv_array, 2, MPI_DOUBLE, MPI_SUM, monitor->sa_master_comm);

    // double some_shifted = recv_array[0];
    // double total_allocated = recv_array[1];

    // other_poweropt = total_allocated - my_poweropt;

    // if (monitor->sa_master)
    // {

    //     if (some_shifted > 0 && shifted == 0.0)
    //     {
    //         if (total_allocated > power_cap_all_nodes)
    //         {
    //             shift_extra = total_allocated - power_cap_all_nodes;
    //             power_manager->new_power_node = (allocated_power - shift_extra) / monitor->sa_size;
    //         }
    //         else if (total_allocated < power_cap_all_nodes)
    //         {
    //             shift_extra = power_cap_all_nodes - total_allocated;
    //             power_manager->new_power_node = (allocated_power + shift_extra) / monitor->sa_size;
    //         }
    //     }
    //     else if (some_shifted == 0.0 && total_allocated < power_cap_all_nodes) //no one shifted but there is power left over
    //         power_manager->new_power_node = my_poweropt / monitor->sa_size;
    // }


    //printf("count: %d, I reached the barrier/bcast: %d, sa_master: %d\n", power_manager->count, monitor->world_rank, monitor->sa_master);
    //MPI_Barrier(monitor->monitors_comm);



    //MPI_Bcast(&power_manager->new_power_node, 1, MPI_DOUBLE, 0, monitor->sa_comm);

    //setup_timer();//restart_timer();

    //MPI_Barrier(monitor->monitors_comm);
    return ret;
}


void power_manager_off (void)
{
    power_manager->off = 1;
}


int integrate_communicators(struct monitor_t * monitor, int this_color, int my_app_rank, MPI_Group app_group, MPI_Comm app_comm)
{
    int ret = 0; //TODO some error handling
    MPI_Group_intersection(app_group, monitor->monitors_group, &monitor->sa_group);

    MPI_Comm_create(MPI_COMM_WORLD, monitor->sa_group, &monitor->sa_comm);

    MPI_Group_rank(monitor->sa_group, &monitor->sa_rank);
    MPI_Group_size(monitor->sa_group, &monitor->sa_size);

    SA_allcomm = app_comm;
    MPI_Comm_size(SA_allcomm, &SA_allsize);

    monitor->sa_master = 0;
    monitor->sa_master_rank = 0;

    if (monitor->sa_rank == 0)
        monitor->sa_master = 1;


    int color;
    if (monitor->sa_master)
        color = 0;
    else
        color = 1;//MPI_UNDEFINED;


    MPI_Comm_split(MPI_COMM_WORLD, color, monitor->world_rank, &monitor->sa_master_comm);
    if (monitor->sa_master)
    {
        MPI_Comm_rank(monitor->sa_master_comm, &monitor->sa_master_rank);
        MPI_Comm_size(monitor->sa_master_comm, &monitor->sa_master_size);
    }

    //let everyone know what the master rank ordering is per S/A partition
    //MPI_Bcast(&monitor->sa_master_rank, 1, MPI_INT, 0, monitor->sa_comm);
    if (monitor->imonitor)
    {
        int my_master_rank;
        MPI_Allreduce(&monitor->sa_master_comm, &my_master_rank, 1, MPI_INT, MPI_LOR, monitor->sa_comm);
    }

    //if (monitor->sa_master)
    //    printf("I am sa master rank: %d, sa master rank: %d, sa master size: %d\n", monitor->sa_master, monitor->sa_master_rank, monitor->sa_master_size);

    return ret;
}

void set_palloc_freq (int freq, struct monitor_t * monitor)
{
    if (monitor->imonitor)
        power_manager->freq = freq; 
}

static void record_allocated_time (struct system_info_t * system_info, struct monitor_t * monitor)
{
    if (monitor->imonitor)
    {
        int log_count = power_manager->log_count;
        if (log_count > 0)
            log_count = log_count - 1;
        struct pm_log_t *log = &power_manager->logs[log_count];
        log->time_allocated = get_time();
        if (log->time_allocated >= system_info->initial_mpi_wtime)
            log->time_allocated = log->time_allocated - system_info->initial_mpi_wtime;
        else
            log->time_allocated = 0;
    }
}

void sync_step_end(struct monitor_t * monitor, struct system_info_t * system_info, struct poller_t * poller)
{
    if (!power_manager->off)
        MPI_Barrier(MPI_COMM_WORLD);
    if (monitor->imonitor)
        poli_log(TRACE, monitor, " entered %s, count : %d\n", __FUNCTION__, power_manager->count);
    int allocate_power = 0;

    if (power_manager->GEOPMLike)
        allocate_power = 1;

    if (power_manager->count % power_manager->freq == 0 && power_manager->count > 0)
        allocate_power = 1;

    if (power_manager->count % power_manager->freq == 0 && monitor->imonitor)
        system_info->palloc_count++;

    if (allocate_power && monitor->imonitor && !power_manager->off)
    {
        if (power_manager->new_power_node > 0.3 * system_info->power_info.package_minimum_power && power_manager->new_power_node < 1.3 * system_info->power_info.package_maximum_power)
        {
            record_allocated_time(system_info, monitor);
            poli_log(DEBUG, NULL, "COUNT=%d, Tstep: %d, Time since start: %f, Rank: %d, Node: %s, allocated power: %f\n", power_manager->count, power_manager->timestep, power_manager->last_sync_time - system_info->initial_mpi_wtime, monitor->world_rank, monitor->my_host, power_manager->new_power_node);
            if (!power_manager->simulate)
                poli_set_power_cap(power_manager->new_power_node);
        }
        if (!power_manager->use_sync_window && power_manager->count + 1 < MAX_POLL_SAMPLES)
        {
            sync_end_measurements(system_info, monitor, poller);
            //MPI_Barrier(MPI_COMM_WORLD);
        }
    }

    if (power_manager->count + 1 < MAX_POLL_SAMPLES)
    {
        if (power_manager->use_sync_window || power_manager->count == 0)
        {
            sync_end_measurements(system_info, monitor, poller);
            //MPI_Barrier(MPI_COMM_WORLD);
        }
        power_manager->count++;
    }

    if (monitor->imonitor)
        poli_log(TRACE, monitor, " finished %s, count : %d, timestep: %d\n", __FUNCTION__, power_manager->count, power_manager->timestep);

}

void update_timestep(int timestep)
{
    power_manager->timestep = timestep;
}

void get_outlier_ranks (double local_time, double global_time, int * rankID, int * nodeID,
    struct monitor_t * monitor)
{
    int this_outlier_rank = 0;
    int this_outlier_node = 0;
    int outliers[3];

    int found = 0;

    if (local_time == global_time)
    {
        this_outlier_rank = monitor->world_rank;
        this_outlier_node = monitor->color;
        found = 1;
    }

    int these_outliers[3] = {this_outlier_rank, this_outlier_node, found};

    MPI_Allreduce(&these_outliers, &outliers, 3, MPI_INT, MPI_SUM, MPI_COMM_WORLD);

    *nodeID = -1;
    *rankID = -1;

    if (outliers[2])
    {
        *nodeID = outliers[1];
        *rankID = outliers[0];
    }

}

//end palloc meas t2
void sync_start_measurements(struct system_info_t * system_info, struct monitor_t * monitor, struct poller_t * poller)
{
    int time_counter = -1;
    if (monitor->imonitor)
    {
        time_counter = poller->time_counter;
        poli_log(TRACE, monitor, " %d entered %s, count : %d\n", monitor->world_rank, __FUNCTION__, power_manager->count);
    }
    double current_time, reduced_time_sa_nodes;
    current_time = get_time();

    current_runtimes[monitor->node_rank] = current_time;
    //poli_log(DEBUG, monitor, "current_runtimes[%d] = %lf, count: %d\n", monitor->node_rank, current_runtimes[monitor->node_rank], power_manager->count);
    int count = power_manager->count;
    struct power_manager_t *palloc_entry;

    if (monitor->imonitor)
    {
        palloc_entry = &system_info->palloc_list[count];
        palloc_entry->current_energy = read_current_energy(system_info); //read energy now before waiting for allreduce

        palloc_entry->my_current_time = current_time;
        palloc_entry->sync_begin = time_counter;
    }

    MPI_Allreduce(&current_time, &reduced_time_sa_nodes, 1, MPI_DOUBLE, MPI_MAX, SA_allcomm);

        // int rankID;
    // int nodeID;
    // get_outlier_ranks(current_time, reduced_time_sa_nodes, &rankID, &nodeID, monitor);

    if (monitor->imonitor)
    {
         palloc_entry->current_time = reduced_time_sa_nodes;
    //     palloc_entry->last_node = nodeID;
    //     palloc_entry->last_rank = rankID;

        double total_time = palloc_entry->current_time - palloc_entry->last_sync_time;

        if (!total_time)
            poli_log(WARNING, monitor, "%d %s: total time is zero! my current_time: %lf, last sync time: %lf\n", monitor->world_rank, __FUNCTION__, palloc_entry->my_current_time, palloc_entry->last_sync_time);
        else
        {
            rapl_compute_total_energy(&(palloc_entry->total_energy.rapl_energy), &(palloc_entry->current_energy.rapl_energy), &(palloc_entry->last_energy.rapl_energy));
            rapl_compute_total_power(&(palloc_entry->total_power.rapl_power), &(palloc_entry->total_energy.rapl_energy), total_time);
            
            if (palloc_entry->total_power.rapl_power.package < system_info->power_info.package_minimum_power || palloc_entry->total_power.rapl_power.package > system_info->power_info.package_maximum_power)
            {
                if (count > 0)
                {
                    struct power_manager_t *previous_entry = &system_info->palloc_list[count - 1];
                    palloc_entry->total_power = previous_entry->total_power;
                    palloc_entry->total_energy = previous_entry->total_energy;
                }
                else
                {
                    palloc_entry->total_power.rapl_power.package = power_manager->pcap;
                    palloc_entry->total_energy.rapl_energy.package = ( palloc_entry->total_power.rapl_power.package * total_time );
                }
            }

            int num_poller_entries = palloc_entry->sync_begin - palloc_entry->last_sync + 1;
            double max_power = 0.0;
            if (num_poller_entries > 0)
            {
                
                double poller_powers[num_poller_entries];
                int i, position;
                int count = 0;
                double power_sum = 0.0;
                for (i = palloc_entry->last_sync; i <= palloc_entry->sync_begin; i++)
                {
                    struct system_poll_info *info = &system_info->system_poll_list[i];
                    power_sum += info->computed_power.rapl_power.package;
                    poller_powers[count] = info->computed_power.rapl_power.package;
                    if (info->computed_power.rapl_power.package > max_power)
                        max_power = info->computed_power.rapl_power.package;
                    count++;
                }

                palloc_entry->average_poll_power = power_sum / (double) num_poller_entries;
                palloc_entry->average_poll_energy = palloc_entry->average_poll_power * total_time;
                palloc_entry->median_poll_power = median(count, poller_powers, &position);
            }
            else
            {
                palloc_entry->average_poll_power = 0.0;
                palloc_entry->median_poll_power = 0.0;
                palloc_entry->average_poll_energy = 0.0;
            }
            palloc_entry->max_poll_power = max_power;
        }
        int current = 0;
        if (palloc_entry->sync_begin > 0)
            current = palloc_entry->sync_begin - 1;

        struct rapl_energy energy_end = system_info->system_poll_list[current].current_energy.rapl_energy;
        struct rapl_energy energy_start = system_info->system_poll_list[palloc_entry->last_sync].current_energy.rapl_energy;
        rapl_compute_total_energy(&(palloc_entry->total_poll_energy), &(energy_end), &(energy_start));
        rapl_compute_total_power(&(palloc_entry->total_poll_power), &(palloc_entry->total_poll_energy), palloc_entry->my_current_time - palloc_entry->my_last_sync_time);

        power_manager->sync_begin = palloc_entry->sync_begin;
        power_manager->current_energy = palloc_entry->current_energy;
        power_manager->total_energy = palloc_entry->total_energy;
        power_manager->total_power = palloc_entry->total_power;
        power_manager->average_poll_power = palloc_entry->average_poll_power;
        power_manager->median_poll_power = palloc_entry->median_poll_power;
        power_manager->max_poll_power = palloc_entry->max_poll_power;
        power_manager->current_time = palloc_entry->current_time;
        power_manager->time_sum += (palloc_entry->current_time - palloc_entry->last_sync_time);
        power_manager->power_sum += palloc_entry->total_power.rapl_power.package;

        poli_log(DEBUG, monitor, "%d %s: current_time: %f, total time: %f, total power: %f, poll power: %f, median poll power: %f, max poll power: %f, timestep: %d, power sum: %lf\n", monitor->world_rank, __FUNCTION__, current_time, palloc_entry->my_current_time - palloc_entry->my_last_sync_time, palloc_entry->total_power.rapl_power.package, palloc_entry->average_poll_power, palloc_entry->median_poll_power, palloc_entry->max_poll_power, count, power_manager->power_sum);

        poli_log(TRACE, monitor, " %d finished %s, count : %d\n", monitor->world_rank, __FUNCTION__, power_manager->count);
    
    }

     
}

//start_palloc_meas
void sync_end_measurements(struct system_info_t * system_info, struct monitor_t * monitor, struct poller_t * poller)
{
    if (monitor->imonitor)
    {
        poli_log(TRACE, monitor, " %d entered %s, count : %d\n", monitor->world_rank, __FUNCTION__, power_manager->count);
    }
    double current_time, reduced_time_sa_nodes;
    current_time = get_time();
    last_sync_runtimes[monitor->node_rank] = current_runtimes[monitor->node_rank];
    //poli_log(DEBUG, monitor, "last_sync_runtimes[%d] = %lf, count: %d\n", monitor->node_rank, last_sync_runtimes[monitor->node_rank], power_manager->count + 1);
    if (power_manager->measure_sync_end)
        last_sync_runtimes[monitor->node_rank] = current_time;
    int count = power_manager->count + 1;
    struct power_manager_t *palloc_entry, *previous_entry;
    if (monitor->imonitor)
    {
        palloc_entry = &system_info->palloc_list[count];
        previous_entry = &system_info->palloc_list[count - 1];
        palloc_entry->time_after_alloc = current_time;
        palloc_entry->last_energy = previous_entry->current_energy;
        palloc_entry->my_last_sync_time = previous_entry->my_current_time;
        palloc_entry->last_sync = previous_entry->sync_begin;
        palloc_entry->last_sync_time = previous_entry->current_time;
        if (power_manager->measure_sync_end)
        {
            palloc_entry->last_energy = read_current_energy(system_info); //read energy now before waiting for allreduce
            palloc_entry->last_sync = poller->time_counter;
            palloc_entry->my_last_sync_time = current_time;
        }
    }

    if (power_manager->measure_sync_end)
    {
        MPI_Allreduce(&current_time, &reduced_time_sa_nodes, 1, MPI_DOUBLE, MPI_MIN, SA_allcomm);

        // int rankID;
        // int nodeID;
        // get_outlier_ranks(current_time, reduced_time_sa_nodes, &rankID, &nodeID, monitor);

         if (monitor->imonitor)
         {
             palloc_entry->last_sync_time = reduced_time_sa_nodes;
        //     palloc_entry->first_node = nodeID;
        //     palloc_entry->first_rank = rankID;
             //power_manager->last_sync_time = palloc_entry->last_sync_time;
         }
    }

    if (monitor->imonitor)
    {

        palloc_entry->count = power_manager->count;
        palloc_entry->node = monitor->color;
        palloc_entry->rank = monitor->world_rank;
        palloc_entry->pcap = power_manager->new_power_node;

        power_manager->last_sync = palloc_entry->last_sync;
        power_manager->last_energy = palloc_entry->last_energy;
        power_manager->last_sync_time = palloc_entry->last_sync_time;

        if (power_manager->count % power_manager->freq == 0) // || !power_manager->use_sync_window)
        {
            power_manager->time_sum = 0;
            power_manager->power_sum = 0;
        }

        poli_log(DEBUG, monitor, "%d %s: last_sync_time: %f, timestep: %d\n", monitor->world_rank, __FUNCTION__, current_time, count);
        poli_log(TRACE, monitor, " %d finished %s, count : %d\n", monitor->world_rank, __FUNCTION__, power_manager->count);
    }


}

static double torben(double m[], int n)
{
    int i, less, greater, equal;
    double  min, max, guess, maxltguess, mingtguess;

    min = max = m[0] ;
    for (i=1 ; i<n ; i++) {
        if (m[i]<min) min=m[i];
        if (m[i]>max) max=m[i];
    }

    while (1) {
        guess = (min+max)/2;
        less = 0; greater = 0; equal = 0;
        maxltguess = min ;
        mingtguess = max ;
        for (i=0; i<n; i++) {
            if (m[i]<guess) {
                less++;
                if (m[i]>maxltguess) maxltguess = m[i] ;
            } else if (m[i]>guess) {
                greater++;
                if (m[i]<mingtguess) mingtguess = m[i] ;
            } else equal++;
        }
        if (less <= (n+1)/2 && greater <= (n+1)/2) break ;
        else if (less>greater) max = maxltguess ;
        else min = mingtguess;
    }
    if (less >= (n+1)/2) return maxltguess;
    else if (less+equal >= (n+1)/2) return guess;
    else return mingtguess;
}


static double median (int n, double x[], int * position)
{
    double temp;
    int i, j;
    // the following two loops sort the array x in ascending order
    if (n == 0)
    {
        *position = 0;
        return 0;
    }
    if (n == 1)
    {
        *position = 0;
        return x[0];
    }
    for (i = 0; i < n - 1; i++) {
        for (j = i + 1; j < n; j++) {
            if (x[j] < x[i])
            {
                // swap elements
                temp = x[i];
                x[i] = x[j];
                x[j] = temp;
            }
        }
    }
    double max;
    if (n == 2)
    {
        *position = 0;
        max = x[0];
        if (x[1] > max)
        {
            *position = 1;
            max = x[1];
        }
        return max;
    }

    *position = n / 2;
    if (n % 2 == 0) {
        max = x[n / 2];
        if (x[(n / 2) - 1] > max)
        {
            max = x[(n / 2) - 1];
            *position = (n / 2) - 1;
        }
        if (x[(n / 2) + 1] > max)
        {
            max = x[(n / 2) + 1];
            *position = (n / 2) + 1;
        }
        return max;
    } else {
        // else return the element in the middle
        return x[n / 2];
    }
}

static void median_energy (int n, uint64_t x[], int * position)
{
    uint64_t temp;
    int i, j;
    // the following two loops sort the array x in ascending order
    if (n == 0)
    {
        *position = 0;
        return;
    }
    if (n == 1)
    {
        *position = 0;
        return;
    }
    for (i = 0; i < n - 1; i++) {
        for (j = i + 1; j < n; j++) {
            if (x[j] < x[i])
            {
                // swap elements
                temp = x[i];
                x[i] = x[j];
                x[j] = temp;
            }
        }
    }
    uint64_t max;
    if (n == 2)
    {
        *position = 0;
        max = x[0];
        if (x[1] > max)
        {
            *position = 1;
            max = x[1];
        }
        return;
    }

    *position = n / 2;
    if (n % 2 == 0) {
        max = x[n / 2];
        if (x[(n / 2) - 1] > max)
        {
            max = x[(n / 2) - 1];
            *position = (n / 2) - 1;
        }
        if (x[(n / 2) + 1] > max)
        {
            max = x[(n / 2) + 1];
            *position = (n / 2) + 1;
        }
        return;
    }
}

void finalize_SeeSAw (struct system_info_t * system_info, struct monitor_t * monitor)
{
    if (monitor->imonitor)
    {
        FILE *fp;
        int i;
        if (power_manager->log_count > 0)
        {
            fp = open_file("PoLiMEr_allocation-logs", monitor);
            if (fp == NULL)
                return;

            fprintf(fp, "Count\tCount Allocation\tSim Timestep\tRank\t");
            fprintf(fp, "My Alpha\tOther Alpha\tRatio\tAdjusted OPT Power (W)\tMy OPT Power (W)\tOther OPT Power (W)\t");
            fprintf(fp, "Allocated Power (W)\tNew Power Node (W)\tPrevious Power (W)\t");
            fprintf(fp, "Max Observed Power (W)\tObserved Power All Nodes (W)\tPCap (W)\t");
            fprintf(fp, "Start Time (s)\tEnd Time (s)\tAverage Time (s)\t");
            fprintf(fp, "Time Allocated (s)\n");

            
            for (i = 0; i < power_manager->log_count; i++)
            {
                struct pm_log_t *log = &power_manager->logs[i];
                fprintf(fp, "%d\t%d\t%d\t%d\t", log->pm_count, log->palloc_count, log->timestep, log->world_rank);
                fprintf(fp, "%lf\t%lf\t%lf\t%lf\t%lf\t%lf\t", log->my_alpha, log->other_alpha, log->ratio, log->adjusted_opt_power, log->my_opt_power, log->other_opt_power);
                fprintf(fp, "%lf\t%lf\t%lf\t", log->allocated_power, log->new_power_per_node, log->previous_total_power);
                fprintf(fp, "%lf\t%lf\t%lf\t", log->max_observed_power, log->observed_power_all_nodes, log->pcap_all_nodes);
                fprintf(fp, "%lf\t%lf\t%lf\t", log->start_time, log->end_time, log->average_time);
                fprintf(fp, "%lf\n", log->time_allocated);
            }

            fclose(fp);
        }
    }
}

void finalize_sync_measurements (struct system_info_t * system_info, struct monitor_t * monitor)
{
    if (monitor->imonitor)
    {
        FILE *fp;
        int i;
        if (power_manager->count > 0)
        {            
            fp = open_file("PoLiMEr_measurements-logs", monitor);
            if (fp == NULL)
                return;

            fprintf(fp, "Count\tPoll Last\tPoll Current\tRank\tNode\t");
            fprintf(fp, "My Last Time (s)\tMy Current Time (s)\tTotal Time (s)\t");
            fprintf(fp, "Time after alloc\t");
            fprintf(fp, "Last Package Energy (J)\tLast DRAM Energy (J)\t");
            fprintf(fp, "Current Package Energy (J)\tCurrent DRAM Energy (J)\t");
            fprintf(fp, "Total Package Power (W)\tTotal DRAM Power (W)\t");
            fprintf(fp, "Total Poll Energy (J)\t");
            fprintf(fp, "Average Poll Power (W)\tMedian Poll Power (W)\tMax Poll Power (W)\tTotal Poll Power (W)\n");

            for (i = 0; i < power_manager->count; i++)
            {
                struct power_manager_t *palloc_entry = &system_info->palloc_list[i];
                fprintf(fp, "%d\t%d\t%d\t%d\t%d\t", i, palloc_entry->last_sync, palloc_entry->sync_begin, palloc_entry->rank, palloc_entry->node);
                fprintf(fp, "%lf\t%lf\t%lf\t", palloc_entry->my_last_sync_time - system_info->initial_mpi_wtime, palloc_entry->my_current_time - system_info->initial_mpi_wtime, palloc_entry->my_current_time - palloc_entry->my_last_sync_time);
                fprintf(fp, "%lf\t", palloc_entry->time_after_alloc - system_info->initial_mpi_wtime);
                fprintf(fp, "%lf\t%lf\t", palloc_entry->last_energy.rapl_energy.package, palloc_entry->last_energy.rapl_energy.dram);
                fprintf(fp, "%lf\t%lf\t", palloc_entry->current_energy.rapl_energy.package, palloc_entry->current_energy.rapl_energy.dram);
                fprintf(fp, "%lf\t%lf\t", palloc_entry->total_power.rapl_power.package, palloc_entry->total_power.rapl_power.dram);
                fprintf(fp, "%lf\t", palloc_entry->total_poll_energy.package);
                fprintf(fp, "%lf\t%lf\t%lf\t%lf\n", palloc_entry->average_poll_power, palloc_entry->median_poll_power, palloc_entry->max_poll_power, palloc_entry->total_poll_power.package);
            }

            fclose(fp);
        }
    }
}

void finalize_SLURMLike (struct system_info_t * system_info, struct monitor_t * monitor)
{
    if (monitor->imonitor)
    {
        FILE *fp;
        int i;
        if (power_manager->log_count > 0)
        {
            fp = open_file("PoLiMEr_allocation-logs", monitor);
            if (fp == NULL)
                return;

            fprintf(fp, "Count\tCount Allocation\tSim Timestep\tRank\t");
            fprintf(fp, "New Power Node (W)\tSlack Power (W)\tPower Cap (W)\t");
            fprintf(fp, "At Power Cap\tObserved Power (W)\t");
            fprintf(fp, "Sync Time (s)\tTime Allocated (s)\n");

            
            for (i = 0; i < power_manager->log_count; i++)
            {
                struct pm_log_t *log = &power_manager->logs[i];
                fprintf(fp, "%d\t%d\t%d\t%d\t", log->pm_count, log->palloc_count, log->timestep, log->world_rank);
                fprintf(fp, "%lf\t%lf\t%lf\t", log->new_power_per_node, log->slack_power, log->pcap_all_nodes);
                fprintf(fp, "%d\t%lf\t", log->at_pcap, log->max_observed_power);
                fprintf(fp, "%lf\t%lf\n", log->average_time, log->time_allocated);
            }

            fclose(fp);
        }
    }
}

void finalize_GEOPMLike (struct system_info_t * system_info, struct monitor_t * monitor)
{
    if (monitor->imonitor)
    {
        FILE *fp;
        int i;
        if (power_manager->log_count > 0)
        {
            fp = open_file("PoLiMEr_allocation-logs", monitor);
            if (fp == NULL)
                return;

            fprintf(fp, "Count\tCount Allocation\tSim Timestep\tRank\t");
            fprintf(fp, "New Power Node (W)\tSlack Power (W)\tExtra Power (W)\t");
            fprintf(fp, "Sync Time (s)\tMedian Node Runtime (s)\tTarget Runtime (s)\tMedian Runtime Rank\t");
            fprintf(fp, "Delta (W)\tObserved Power (W)\tTarget Met\tTime Allocated (s)\n");

            
            for (i = 0; i < power_manager->log_count; i++)
            {
                struct pm_log_t *log = &power_manager->logs[i];
                fprintf(fp, "%d\t%d\t%d\t%d\t", log->pm_count, log->palloc_count, log->timestep, log->world_rank);
                fprintf(fp, "%lf\t%lf\t%lf\t", log->new_power_per_node, log->slack_power, log->extra_power);
                fprintf(fp, "%lf\t%lf\t%lf\t%d\t", log->average_time, log->median_node_runtime, log->target_runtime, log->median_runtime_rank);
                fprintf(fp, "%lf\t%lf\t%d\t%lf\n", log->delta, log->max_observed_power, log->target_met, log->time_allocated);
            }

            fclose(fp);
        }
    }
}

void finalize_power_manager (struct system_info_t * system_info, struct monitor_t * monitor)
{
    if (power_manager->SeeSAw)
        finalize_SeeSAw(system_info, monitor);
    else if (power_manager->SLURMLike)
        finalize_SLURMLike(system_info, monitor);
    else if (power_manager->GEOPMLike)
        finalize_GEOPMLike(system_info, monitor);

    finalize_sync_measurements(system_info, monitor);

    return;
}