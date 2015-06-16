#include "sched.h"
#include "schedproc.h"

#include <assert.h>
#include <minix/com.h>
#include <timers.h>
#include <machine/archtypes.h>
#include <sys/resource.h> /* for PRIO_MAX & PRIO_MIN */
#include "kernel/proc.h" /* for queue constants */

#define SCHEDULE_CHANGE_PRIO	0x1
#define SCHEDULE_CHANGE_QUANTUM	0x2
#define SCHEDULE_CHANGE_CPU	0x4

#define SCHEDULE_CHANGE_ALL	(	\
		SCHEDULE_CHANGE_PRIO	|	\
		SCHEDULE_CHANGE_QUANTUM	|	\
		SCHEDULE_CHANGE_CPU		\
		)

#define CPU_DEAD	-1

#define DEFAULT_USER_TIME_SLICE 200
#define INC_PER_QUEUE 10

unsigned cpu_proc[CONFIG_MAX_CPUS];

#define cpu_is_available(c)	(cpu_proc[c] >= 0)
#define is_system_proc(p)	((p)->parent == RS_PROC_NR)

extern "C" int call_minix_sys_schedule(endpoint_t proc_ep, int priority, int quantum, int cpu);
extern "C" int call_minix_sys_schedctl(unsigned flags, endpoint_t proc_ep, int priority, int quantum, int cpu);
extern "C" int accept_message(message *m_ptr);
extern "C" int no_sys(int who_e, int call_nr);
int sched_isokendpt(int endpoint, int *proc);
int sched_isemptyendpt(int endpoint, int *proc);

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

extern "C" int Schedproc::do_noquantum(unsigned ipc) 
{
	int rv;
	unsigned burst, queue_bump;

	burst = (this->time_slice * 1000 / ipc) / 100;
 
	burst = this->burst_smooth(burst);

	queue_bump = burst/INC_PER_QUEUE;

	if (this->max_priority + queue_bump > MIN_USER_Q) {
		queue_bump = MIN_USER_Q - this->max_priority;
	}

	this->priority = this->max_priority + queue_bump;
	this->time_slice = this->base_time_slice + 2 * queue_bump * (this->base_time_slice/10);

	if ((rv = this->schedule_process(SCHEDULE_CHANGE_PRIO | SCHEDULE_CHANGE_QUANTUM)) != OK) {
		return rv;
	}
	return OK;
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

extern "C" int Schedproc::do_stop_scheduling()
{
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
	if(endpoint != schedproc[*proc].endpoint)
		return (EDEADEPT);
	if(!(schedproc[*proc].flags & IN_USE))
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
	if(schedproc[*proc].flags & IN_USE)
		return (EDEADEPT);
	return (OK);
}

extern "C" int Schedproc::do_nice(unsigned new_q)
{
	int rv;
	unsigned old_q, old_max_q;

	if (new_q >= NR_SCHED_QUEUES) {
		return EINVAL;
	}

	/* Store old values, in case we need to roll back the changes */
	old_q     = this->priority;
	old_max_q = this->max_priority;

	/* Update the proc entry and reschedule the process */
	this->max_priority = this->priority = new_q;

	if ((rv = this->schedule_process(SCHEDULE_CHANGE_PRIO | SCHEDULE_CHANGE_QUANTUM)) != OK) 
	{
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

	if ((err = call_minix_sys_schedule(this->endpoint, new_prio,
		new_quantum, new_cpu)) != OK) {
		printf("PM: An error occurred when trying to schedule %d: %d\n",this->endpoint, err);
	}

	return err;
}

extern "C" int Schedproc::do_start_scheduling(int mtp, endpoint_t endp, endpoint_t parent, unsigned maxprio, unsigned quantum)
{
	int rv, parent_nr_n;
	
	/* we can handle two kinds of messages here */
	assert(mtp == SCHEDULING_START || mtp == SCHEDULING_INHERIT);

	/* Populate process slot */
	this->endpoint     = endp;
	this->parent       = parent;
	this->max_priority = maxprio;
	this->burst_hist_cnt = 0;
	if (this->max_priority >= NR_SCHED_QUEUES) {
		return EINVAL;
	}

	/* Inherit current priority and time slice from parent. Since there
	 * is currently only one scheduler scheduling the whole system, this
	 * value is local and we assert that the parent endpoint is valid */
	if (this->endpoint == this->parent) {
		/* We have a special case here for init, which is the first
		   process scheduled, and the parent of itself. */
		this->priority   = USER_Q;
		this->time_slice = DEFAULT_USER_TIME_SLICE;

		/*
		 * Since kernel never changes the cpu of a process, all are
		 * started on the BSP and the userspace scheduling hasn't
		 * changed that yet either, we can be sure that BSP is the
		 * processor where the processes run now.
		 */
#ifdef CONFIG_SMP
		this->cpu = machine.bsp_id;
		/* FIXME set the cpu mask */
#endif
	}
	
	switch (mtp) {

	case SCHEDULING_START:
		/* We have a special case here for system processes, for which
		 * quanum and priority are set explicitly rather than inherited 
		 * from the parent */
		this->priority   = this->max_priority;
		this->time_slice = quantum;
		this->base_time_slice = this->time_slice;
		break;
		
	case SCHEDULING_INHERIT:
		/* Inherit current priority and time slice from parent. Since there
		 * is currently only one scheduler scheduling the whole system, this
		 * value is local and we assert that the parent endpoint is valid */
		if ((rv = sched_isokendpt(parent,&parent_nr_n)) != OK)
			return rv;

		this->priority = schedproc[parent_nr_n].priority;
		this->time_slice = schedproc[parent_nr_n].time_slice;
		this->base_time_slice = this->time_slice;
		break;
		
	default: 
		/* not reachable */
		assert(0);
	}

	/* Take over scheduling the process. The kernel reply message populates
	 * the processes current priority and its time slice */
	if ((rv = call_minix_sys_schedctl(0, this->endpoint, 0, 0, 0)) != OK) {
		printf("Sched: Error taking over scheduling for %d, kernel said %d\n",this->endpoint, rv);
		return rv;
	}
	this->flags = IN_USE;

	/* Schedule the process, giving it some quantum */
	this->pick_cpu();
	while ((rv = this->schedule_process(SCHEDULE_CHANGE_ALL)) == EBADCPU) {
		/* don't try this CPU ever again */
		cpu_proc[this->cpu] = CPU_DEAD;
		this->pick_cpu();
	}

	if (rv != OK) {
		printf("Sched: Error while scheduling process, kernel replied %d\n",rv);
		return rv;
	}

	/* Mark ourselves as the new scheduler.
	 * By default, processes are scheduled by the parents scheduler. In case
	 * this scheduler would want to delegate scheduling to another
	 * scheduler, it could do so and then write the endpoint of that
	 * scheduler into SCHEDULING_SCHEDULER
	 */

	//m_ptr->SCHEDULING_SCHEDULER = SCHED_PROC_NR;

	return OK;
}

extern "C" int no_sys(int who_e, int call_nr)
{
/* A system call number not implemented by PM has been requested. */
  printf("SCHED: in no_sys, call nr %d from %d\n", call_nr, who_e);
  return(ENOSYS);
}


extern "C" int accept_message(message *m_ptr)
{
	/* accept all messages from PM and RS */
	switch (m_ptr->m_source) {

		case PM_PROC_NR:
		case RS_PROC_NR:
			return 1;
	}
	
	/* no other messages are allowable */
	return 0;
}

/*===========================================================================*
 *			       decoder					     *
 *===========================================================================*/
extern "C" int decoder(int req, message *m_ptr) 
{
	int rv, proc_nr_n,parent_nr_n;

	if (req != SCHEDULING_NO_QUANTUM) {
		if (!accept_message(m_ptr))
			return EPERM;
	}

	if ( (req == SCHEDULING_INHERIT) || (req == SCHEDULING_START) ) {
		if ((rv = sched_isemtyendpt(m_ptr->SCHEDULING_ENDPOINT, &proc_nr_n)) != OK) {
			return rv;
		}
		if (req == SCHEDULING_INHERIT) {
			if ((rv = sched_isokendpt(m_ptr->SCHEDULING_PARENT,&parent_nr_n)) != OK)
				return rv;
		}
		m_ptr->SCHEDULING_SCHEDULER = SCHED_PROC_NR;
	}
	else {
		if (req == SCHEDULING_STOP) {
			if (sched_isokendpt(m_ptr->SCHEDULING_ENDPOINT, &proc_nr_n) != OK) {
				printf("SCHED: WARNING: got an invalid endpoint in OOQ msg ""%ld\n", m_ptr->SCHEDULING_ENDPOINT);
				return EBADEPT;
			}
		}
		else
		{
			// SCHEDULING_NO_QUANTUM
			if (sched_isokendpt(m_ptr->m_source, &proc_nr_n) != OK) {
				printf("SCHED: WARNING: got an invalid endpoint in OOQ msg %u.\n",m_ptr->m_source);
				return EBADEPT;
			}
		}

	}
	return proc_nr_n;
}

extern "C" int invoke_sched_method(int index, int function, message *m_ptr)
{
	Schedproc *rmp;
	rmp = &schedproc[index];
	int mtp=m_ptr->m_type;
	endpoint_t endp=m_ptr->SCHEDULING_ENDPOINT, parent= m_ptr->SCHEDULING_PARENT;
	unsigned maxprio=m_ptr->SCHEDULING_MAXPRIO, quantum=m_ptr->SCHEDULING_QUANTUM;

	switch(function){
		case SCHEDULING_START:
			//return rmp->do_start_scheduling(m_ptr);
			return rmp->do_start_scheduling(int mtp, endpoint_t endp, endpoint_t parent, unsigned maxprio, unsigned quantum);
		case SCHEDULING_STOP:
			return rmp->do_stop_scheduling();
		case SCHEDULING_SET_NICE:
			return rmp->do_nice((unsigned) m_ptr->SCHEDULING_MAXPRIO);
		case SCHEDULING_NO_QUANTUM:
			return rmp->do_noquantum((unsigned)m_ptr->SCHEDULING_ACNT_IPC_ASYNC + (unsigned)m_ptr->SCHEDULING_ACNT_IPC_SYNC + 1);
	}
	return 0;	
}
