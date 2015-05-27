#include "schedproc.h"


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

//Schedproc::Schedproc(Schedproc src)
//{
	//this->endpoint = src->endpoint;	/* process endpoint id */
	//this->parent = src->parent;		/* parent endpoint id */
	//this->flags = src->flags;			/* flag bits */
//
	///* User space scheduling */
//	this->max_priority = src->max_priority;		/* this process' highest allowed priority */
	//this->priority = src->priority;			/* the process' current priority */
	//this->base_time_slice = src->base_time_slice;
	//this->time_slice = src->time_slice;		/* this process's time slice */
	//this->cpu = src->cpu;
	//this->cpu_mask[BITMAP_CHUNKS(CONFIG_MAX_CPUS)] = src->cpu_mask[BITMAP_CHUNKS(CONFIG_MAX_CPUS)];
	//this->burst_history[BURST_HISTORY_LENGTH] = src->burst_history[BURST_HISTORY_LENGTH];
	//this->burst_hist_cnt = src->burst_hist_cnt;
//}

int Schedproc::burst_smooth(unsigned burst)
{
	int i;
	unsigned avg_burst = 0;

	this->burst_history[this->burst_hist_cnt++ % 
BURST_HISTORY_LENGTH] = burst;

	for (i=0; i<BURST_HISTORY_LENGTH; i++)
	{
		if (i >= this->burst_hist_cnt) {
			break;
		}
		avg_burst += this->burst_history[i];
	}	
	avg_burst /= i;

	return avg_burst;
}

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

	//this = listSched[proc_nr_n];
#ifdef CONFIG_SMP
	cpu_proc[this->cpu]--;
#endif
	this->flags = 0; /*&= ~IN_USE;*/
	return OK;
}

int sched_isokendpt(int endpoint, int *proc)
{
	*proc = _ENDPOINT_P(endpoint);
	if (*proc < 0)
		return (EBADEPT); /* Don't schedule tasks */
	if(*proc >= NR_PROCS)
		return (EINVAL);
	if(endpoint != listSched[*proc].endpoint)
		return (EDEADEPT);
	if(!(listSched[*proc].flags & IN_USE))
		return (EDEADEPT);
	return (OK);
}

int sched_isemtyendpt(int endpoint, int *proc)
{
	*proc = _ENDPOINT_P(endpoint);
	if (*proc < 0)
		return (EBADEPT); /* Don't schedule tasks */
	if(*proc >= NR_PROCS)
		return (EINVAL);
	if(listSched[*proc].flags & IN_USE)
		return (EDEADEPT);
	return (OK);
}
