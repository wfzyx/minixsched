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

void Schedproc::pick_cpu()
{
#ifdef CONFIG_SMP
	unsigned cpu, c;
	unsigned cpu_load = (unsigned) -1;

	if (machine.processors_count == 1) {
		this->cpu = machine.bsp_id;
		return;
	}

	if (is_system_proc(this)) {
		this->cpu = machine.bsp_id;
		return;
	}

	cpu = machine.bsp_id;
	for (c = 0; c < machine.processors_count; c++) {
		if (!cpu_is_available(c))
			continue;
		if (c != machine.bsp_id && cpu_load > cpu_proc[c]) {
			cpu_load = cpu_proc[c];
			cpu = c;
		}
	}
	this->cpu = cpu;
	cpu_proc[cpu]++;
#else
	this->cpu = 0;
#endif
}

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

int Schedproc::sched_isokendpt(int endpoint, int *proc)
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

int Schedproc::sched_isemtyendpt(int endpoint, int *proc)
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


int Schedproc::do_nice(message *m_ptr)
{
	int rv;
	int proc_nr_n;
	unsigned new_q, old_q, old_max_q;

	/* check who can send you requests */
	if (!accept_message(m_ptr))
		return EPERM;

	if (sched_isokendpt(m_ptr->SCHEDULING_ENDPOINT, &proc_nr_n) != OK) {
		printf("SCHED: WARNING: got an invalid endpoint in OOQ msg "
		"%ld\n", m_ptr->SCHEDULING_ENDPOINT);
		return EBADEPT;
	}

	//rmp = &schedproc[proc_nr_n];
	new_q = (unsigned) m_ptr->SCHEDULING_MAXPRIO;
	if (new_q >= NR_SCHED_QUEUES) {
		return EINVAL;
	}

	/* Store old values, in case we need to roll back the changes */
	old_q     = this->priority;
	old_max_q = this->max_priority;

	/* Update the proc entry and reschedule the process */
	this->max_priority = this->priority = new_q;

// TODO: Check from where flags should come
        unsigned flags_placeholder = 0;
	if ((rv = this->schedule_process(flags_placeholder)) != OK) 
{
		/* Something went wrong when rescheduling the process, roll
		 * back the changes to proc struct */
		this->priority     = old_q;
		this->max_priority = old_max_q;
	}

	return rv;
}

int Schedproc::schedule_process(unsigned flags)
{
	int err;
	int new_prio, new_quantum, new_cpu;

	this->pick_cpu();

	if (flags & SCHEDULE_CHANGE_PRIO)
		new_prio = this->priority;
	else
		new_prio = -1;

	if (flags & SCHEDULE_CHANGE_QUANTUM)
		new_quantum = this->time_slice;
	else
		new_quantum = -1;

	if (flags & SCHEDULE_CHANGE_CPU)
		new_cpu = this->cpu;
	else
		new_cpu = -1;

	if ((err = sys_schedule(this->endpoint, new_prio,
		new_quantum, new_cpu)) != OK) {
		printf("PM: An error occurred when trying to schedule %d: %d\n",
		this->endpoint, err);
	}

	return err;
}
