// // TODO REMOVE UNNECESSARY STMTS

// /* This table has one slot per process.  It contains scheduling information
//  * for each process.
//  */
// #include <limits.h>

// #include <minix/bitmap.h>

// /* EXTERN should be extern except in main.c, where we want to keep the struct */
// #ifdef _MAIN
// #undef EXTERN
// #define EXTERN
// #endif

// #ifndef CONFIG_SMP
// #define CONFIG_MAX_CPUS 1
// #endif

// /**
//  * We might later want to add more information to this table, such as the
//  * process owner, process group or cpumask.
//  */
// #define BURST_HISTORY_LENGTH 10
// EXTERN struct schedproc {
// //struct schedproc {
// 	endpoint_t endpoint;	/* process endpoint id */
// 	endpoint_t parent;	/* parent endpoint id */
// 	unsigned flags;		/* flag bits */

// 	/* User space scheduling */
// 	unsigned max_priority;	/* this process' highest allowed priority */
// 	unsigned priority;		/* the process' current priority */
// 	unsigned base_time_slice;
// 	unsigned time_slice;		/* this process's time slice */
// 	unsigned cpu;
// 	bitchunk_t cpu_mask[BITMAP_CHUNKS(CONFIG_MAX_CPUS)];

// 	unsigned burst_history[BURST_HISTORY_LENGTH];
// 	unsigned burst_hist_cnt;
// } schedproc[NR_PROCS];

// /* Flag values */

// TODO: CHECK THESE INCLUDES

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
	    // void setValues(struct schedproc argStruct);
	    // struct schedproc toStruct ();
	    // Schedproc(Schedproc &cSrc);
	    void pick_cpu();
	    int burst_smooth(unsigned burst);
	    int do_stop_scheduling(message *m_ptr);
	    int sched_isokendpt(int endpoint, int *proc);
	    int sched_isemtyendpt(int endpoint, int *proc);
	    int schedule_process(unsigned flags);
	    int do_nice(message *m_ptr);
};

extern Schedproc listSched[NR_PROCS];

#else
typedef struct schedproc{} sched;
extern struct schedproc schedproc[NR_PROCS];
#endif

#endif
