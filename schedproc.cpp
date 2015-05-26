#include "sched.h"
#include "schedproc.h"

class Schedproc {

    endpoint_t endpoint;	/* process endpoint id */
	endpoint_t parent;		/* parent endpoint id */
	unsigned flags;			/* flag bits */

	/* User space scheduling */
	unsigned max_priority;		/* this process' highest allowed priority */
	unsigned priority;			/* the process' current priority */
	unsigned base_time_slice;
	unsigned time_slice;		/* this process's time slice */
	unsigned cpu;
//	bitchunk_t cpu_mask[BITMAP_CHUNKS(CONFIG_MAX_CPUS)];

	unsigned burst_history[BURST_HISTORY_LENGTH];
	unsigned burst_hist_cnt;
  public:
    Schedproc (schedproc argStruct);
    schedproc toStruct ();

};


schedproc Schedproc::toStruct(){

	schedproc retStruct;
	retStruct.endpoint = endpoint;
	retStruct.parent = parent;
	retStruct.flags = flags;
	retStruct.max_priority = max_priority;
	retStruct.priority = priority;
	retStruct.base_time_slice = base_time_slice;
	retStruct.time_slice = time_slice;
	retStruct.cpu = cpu;
//	retStruct.cpu_mask[BITMAP_CHUNKS = cpu_mask[BITMAP_CHUNKS(CONFIG_MAX_CPUS)];
	retStruct.burst_history[BURST_HISTORY_LENGTH] = burst_history[BURST_HISTORY_LENGTH];
	retStruct.burst_hist_cnt = burst_hist_cnt;

}

Schedproc::Schedproc (schedproc argStruct) {

	endpoint = argStruct.endpoint;
	parent = argStruct.parent;
	flags = argStruct.flags;
	max_priority = argStruct.max_priority;
	priority = argStruct.priority;
	base_time_slice = argStruct.base_time_slice;
	time_slice = argStruct.time_slice;
	cpu = argStruct.cpu;
//	cpu_mask[BITMAP_CHUNKS = 
argStruct.cpu_mask[BITMAP_CHUNKS(CONFIG_MAX_CPUS)];
	burst_history[BURST_HISTORY_LENGTH] = argStruct.burst_history[BURST_HISTORY_LENGTH];
	burst_hist_cnt = argStruct.burst_hist_cnt;

}
