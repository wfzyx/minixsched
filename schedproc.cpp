#include "c_schedproc.h"


// TODO: Check struct call to C typedef
// struct schedproc Schedproc::toStruct()
// {
// 	struct schedproc retStruct;
// 	retStruct.endpoint = endpoint;
// 	retStruct.parent = parent;
// 	retStruct.flags = flags;
// 	retStruct.max_priority = max_priority;
// 	retStruct.priority = priority;
// 	retStruct.base_time_slice = base_time_slice;
// 	retStruct.time_slice = time_slice;
// 	retStruct.cpu = cpu;
// 	retStruct.cpu_mask[BITMAP_CHUNKS] = cpu_mask[BITMAP_CHUNKS(CONFIG_MAX_CPUS)];
// 	retStruct.burst_history[BURST_HISTORY_LENGTH] = burst_history[BURST_HISTORY_LENGTH];
// 	retStruct.burst_hist_cnt = burst_hist_cnt;
// 	return retStruct;
// }


// void Schedproc::setValues (struct schedproc argStruct)
// {
// 	endpoint = argStruct.endpoint;
// 	parent = argStruct.parent;
// 	flags = argStruct.flags;
// 	max_priority = argStruct.max_priority;
// 	priority = argStruct.priority;
// 	base_time_slice = argStruct.base_time_slice;
// 	time_slice = argStruct.time_slice;
// 	cpu = argStruct.cpu;
// 	cpu_mask[BITMAP_CHUNKS = argStruct.cpu_mask[BITMAP_CHUNKS(CONFIG_MAX_CPUS)];
// 	burst_history[BURST_HISTORY_LENGTH] = argStruct.burst_history[BURST_HISTORY_LENGTH];
// 	burst_hist_cnt = argStruct.burst_hist_cnt;
// }

int Schedproc::do_stop_scheduling(message *m_ptr)
{
	int proc_nr_n;

	/* check who can send you requests */
	if (!accept_message(m_ptr))
		return EPERM;

	if (sched_isokendpt(m_ptr->SCHEDULING_ENDPOINT, &proc_nr_n) != OK) {
		printf("SCHED: WARNING: got an invalid endpoint in OOQ msg "
		"%ld\n", m_ptr->SCHEDULING_ENDPOINT);
		return EBADEPT;
	}

	this = listSched[proc_nr_n];
#ifdef CONFIG_SMP
	cpu_proc[this->cpu]--;
#endif
	this->flags = 0; /*&= ~IN_USE;*/
	return OK;
}