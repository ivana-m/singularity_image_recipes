#ifndef __MPI_HANDLER_H
#define __MPI_HANDLER_H

#ifdef __cplusplus
extern "C"
{
#endif

struct monitor_t;

int mpi_init (struct monitor_t * monitor);
void barrier (struct monitor_t * monitor);
void barrier_node (struct monitor_t * monitor);
int is_finalized (void);
void organize_ranks (struct monitor_t * monitor);

#ifdef __cplusplus
}
#endif

#endif