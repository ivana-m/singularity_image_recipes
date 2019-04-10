#include <stdio.h>
#include <sched.h>
#include <utmpx.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "mpi.h"
#include "omp.h"

#include "PoLiMEr.h"

int main(int argc, char ** argv)
{
    int proc_id, numprocs, thread_id, numthreads;

#ifdef _MPI
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &proc_id);
    MPI_Comm_size(MPI_COMM_WORLD, &numprocs);

    poli_init();

#else
    proc_id = 0;
    numprocs = 0;
#endif

    numthreads = omp_get_num_threads();


    double watts1 = 220.0;
    if (proc_id == 0)
	    printf("setting pcap\n");
    poli_set_power_cap(watts1);
    if (proc_id == 0)
	    printf("done setting pcap\n");
     
    int i;
    for (i = 0; i < 5; i++)
    {
        char *tag_name = "hello_world"; //tag_name[15];
        //sprintf(tag_name, "hello_tag-%d", i);
        poli_start_tag("%s-%d", tag_name, i);
        char *zone_name = "PACKAGE";

        double watts;
        poli_get_power_cap(&watts);
        //printf("Rank %d got power cap: %lf\n", proc_id, watts);
        poli_print_power_cap_info();

        #pragma omp parallel private(thread_id)
        {
            thread_id = omp_get_thread_num();
    #ifdef _MPI
            printf("Hello from rank %d, thread %d executing on cpu %d\n", proc_id, thread_id, sched_getcpu());
    #else
            printf("Hello from thread %d executing on cpu %d\n", thread_id, sched_getcpu());
    #endif
            sleep(1);
        }
        
        poli_end_tag("%s-%d", tag_name, i);
    } 
    //printf("Testing reset system\n");
    //emon_reset_system(); 
    poli_finalize();
    printf("Rank %d is done with PoLiMEr\n", proc_id);
#ifdef _MPI
    MPI_Finalize();
#endif

    return 0;
}
