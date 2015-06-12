#ifndef SCHD_H
#define SCHD_H
#define IN_USE		0x00001	/* set when 'schedproc' slot in use */
#include <limits.h>
#include <minix/bitmap.h>
#include "sched.h"

#ifndef CONFIG_SMP
#define CONFIG_MAX_CPUS 1
#endif

#define BURST_HISTORY_LENGTH 10

#ifdef __cplusplus

class Schedproc
{
	public:
	   	endpoint_t endpoint;	/* process endpoint id */
		endpoint_t parent;		/* parent endpoint id */
		unsigned flags;			/* flag bits */

		/* User space scheduling */
		unsigned max_priority;		/* this process' highest allowed priority */
		unsigned priority;			/* the process' current priority */
		unsigned base_time_slice;
		unsigned time_slice;		/* this process's time slice */
		unsigned cpu;
		bitchunk_t cpu_mask[BITMAP_CHUNKS(CONFIG_MAX_CPUS)];
		unsigned burst_history[BURST_HISTORY_LENGTH];
		unsigned burst_hist_cnt;
	    	void pick_cpu();
	    	int burst_smooth(unsigned burst);
	    	int schedule_process(unsigned flags);
 		int do_noquantum(int proc_nr_n);
		int do_stop_scheduling(int proc_nr_n);
		int do_nice(int proc_nr_n);
};
Schedproc schedproc[NR_PROCS];

class DecParam
{
	public:
		unsigned maxprio;
		unsigned acnt_ipc_async;
		unsigned acnt_cpu_load;
};
DecParam decparam;

#else
typedef struct schedproc{} sched;
extern struct schedproc schedproc[NR_PROCS];
#endif

#endif
