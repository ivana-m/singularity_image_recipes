#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <math.h>

#include <mpi.h>
#ifdef _PMI
#include <pmi.h>
#endif

#include "mpi_handler.h"
#include "PoLiLog.h"
#include "PoLiMEr.h"

#ifdef _MSR
#include "msr_handler.h"
#endif

#ifdef _CRAY
#include "cray_handler.h"
#endif

#ifdef _BGQ
#include "bgq_handler.h"
#endif

static int get_comm_split_color (struct monitor_t * monitor);
#ifdef _PMI
static int get_comm_split_color_pmi (struct monitor_t * monitor);
#else
#ifndef _BGQ
static int get_comm_split_color_hostname (struct monitor_t * monitor);
#endif
#endif

int mpi_init (struct monitor_t * monitor)
{
	// get current MPI environment
	MPI_Comm_size(MPI_COMM_WORLD, &monitor->world_size);
	MPI_Comm_rank(MPI_COMM_WORLD, &monitor->world_rank);

	// determine host and color based on host
    monitor->my_host = calloc(MPI_MAX_PROCESSOR_NAME, sizeof(monitor->my_host));
    //memset(monitor->my_host, '\0', sizeof(monitor->my_host));
    get_comm_split_color(monitor);

    // set up new MPI environment
    MPI_Comm_split(MPI_COMM_WORLD, monitor->color, monitor->world_rank, &monitor->mynode_comm);
    MPI_Comm_size(monitor->mynode_comm, &monitor->node_size);
    MPI_Comm_rank(monitor->mynode_comm, &monitor->node_rank);

    if (monitor->node_rank == 0)
        monitor->imonitor = 1;
    else
        monitor->imonitor = 0;

    organize_ranks(monitor);

    return 0;
}

void organize_ranks (struct monitor_t * monitor)
{
    int num_monitors;
    MPI_Allreduce(&monitor->imonitor, &num_monitors, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
    monitor->num_monitors = num_monitors;
    if (num_monitors <= 1)
        return;

    int ranks[monitor->world_size], allranks[monitor->world_size];
    int i;
    for (i = 0; i < monitor->world_size; i++)
        ranks[i] = 0;

    if (monitor->imonitor)
        ranks[monitor->world_rank] = monitor->world_rank;

    MPI_Allreduce(ranks, allranks, monitor->world_size, MPI_INT, MPI_SUM, MPI_COMM_WORLD);

    int monitors[num_monitors];

    int midx = 0;

    for (i = 0; i < monitor->world_size; i++)
    {
        if (allranks[i] != 0 || i == 0)
        {
            monitors[midx] = allranks[i];
            midx++;
        }
    }

    MPI_Group world_group;
    MPI_Comm_group(MPI_COMM_WORLD, &world_group);

    MPI_Group_incl(world_group, num_monitors, monitors, &monitor->monitors_group);

    MPI_Comm_create(MPI_COMM_WORLD, monitor->monitors_group, &monitor->monitors_comm);

    MPI_Group_size(monitor->monitors_group, &monitor->monitors_size);

    return;
}

static int get_comm_split_color (struct monitor_t * monitor)
{
#ifdef _PMI
    return get_comm_split_color_pmi (monitor);
#elif _BGQ
    return get_comm_split_color_bgq (monitor);
#else
    return get_comm_split_color_hostname (monitor);
#endif
    return 0;
}

#ifdef _PMI
/* This does not work when on the debug queue*/
static int get_comm_split_color_pmi (struct monitor_t * monitor)
{
    // identify which rank is on what node
    int coord[4];
    pmi_mesh_coord_t xyz;
    int nid;
    PMI_Get_nid(monitor->world_rank, &nid);
    PMI_Get_meshcoord((pmi_nid_t) nid, &xyz);
    coord[0] = xyz.mesh_x;
    coord[1] = xyz.mesh_y;
    coord[2] = xyz.mesh_z;
    // the nid helps to identify one of the four nodes hosted on a Aries router.
    coord[3] = nid;

    // set sub communicator color to nid
    monitor->color = coord[3];

    snprintf(monitor->my_host, sizeof(monitor->my_host), "%d", monitor->color);
    return 0;
}
#else
#ifndef _BGQ
static int get_comm_split_color_hostname (struct monitor_t * monitor)
{
    char hostname[MPI_MAX_PROCESSOR_NAME];
    int resultlen;
    MPI_Get_processor_name(hostname, &resultlen);

    //the following is a hack for JLSE
    //hostnames should be of the form knlxx.ftm.alcf.anl.gov

    char *token;
    token = strtok(hostname, " \t.\n");
    strncpy(monitor->my_host, token, 5);
    char *hostnum = strtok(monitor->my_host, "knl");
    char clean_hostnum[strlen(hostnum)];
    memset(clean_hostnum, '\0', sizeof(clean_hostnum));
    int i;
    int prev_zero = 0;
    int j = 0;
    for (i = 0; i < strlen(hostnum); i++)
    {
        if (i == 0)
        {
            if (hostnum[i] == '0')
                prev_zero = 1;
        }

        if (hostnum[i] != '0')
        {
            prev_zero = 0;
            clean_hostnum[j] = hostnum[i];
            j++;
        }
        else
        {
            if (!prev_zero)
            {
                clean_hostnum[j] = hostnum[i];
                j++;
            }
        }
    }

    monitor->color = atoi(clean_hostnum);

    return 0;
}
#endif
#endif

int is_finalized (void)
{
	int finalized;
	MPI_Finalized(&finalized);
	return finalized;
}

void barrier (struct monitor_t * monitor)
{
	if (monitor->world_size > 1 && !is_finalized())
		MPI_Barrier(MPI_COMM_WORLD);
}

void barrier_node (struct monitor_t * monitor)
{
	if (monitor->node_size > 1 && !is_finalized())
		MPI_Barrier(monitor->mynode_comm);
}