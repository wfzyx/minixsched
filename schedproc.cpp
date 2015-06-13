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

struct decp
{
	endpoint_t endpoint;
	endpoint_t parent;
	unsigned quantum;
	unsigned maxprio;
	unsigned acnt_ipc_async;
	unsigned acnt_cpu_load;
	int mtype;
	unsigned parent_priority;
	unsigned parent_time_slice;
	message **p; // ponteiro para ponteiro, depois vai resolver "TUDO"
} dec;

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

extern "C" int Schedproc::do_noquantum(int proc_nr_n)
{
	Schedproc *rmp;
	int rv;
	unsigned ipc, burst, queue_bump;
	short load;

	rmp = &schedproc[proc_nr_n];

	ipc = (unsigned)dec.acnt_ipc_async + (unsigned)dec.acnt_ipc_async + 1;

	load = dec.acnt_cpu_load;
	
	burst = (rmp->time_slice * 1000 / ipc) / 100;
 
	burst = rmp->burst_smooth(burst);

	queue_bump = burst/INC_PER_QUEUE;

	if (rmp->max_priority + queue_bump > MIN_USER_Q) {
		queue_bump = MIN_USER_Q - rmp->max_priority;
	}

	rmp->priority = rmp->max_priority + queue_bump;
	rmp->time_slice = rmp->base_time_slice + 2 * queue_bump * (rmp->base_time_slice/10);

	if ((rv = rmp->schedule_process(SCHEDULE_CHANGE_PRIO | SCHEDULE_CHANGE_QUANTUM)) != OK) {
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

extern "C" int Schedproc::do_stop_scheduling(int proc_nr_n)
{
	Schedproc *rmp;

	rmp = &schedproc[proc_nr_n];
#ifdef CONFIG_SMP
	cpu_proc[rmp->cpu]--;
#endif
	rmp->flags = 0; /*&= ~IN_USE;*/

	return OK;
}

extern "C" int Schedproc::do_nice(int proc_nr_n)
{
	Schedproc *rmp;
	int rv;
	unsigned new_q, old_q, old_max_q;

	rmp = &schedproc[proc_nr_n];

	new_q = (unsigned) dec.maxprio;
	if (new_q >= NR_SCHED_QUEUES) {
		return EINVAL;
	}

	/* Store old values, in case we need to roll back the changes */
	old_q     = rmp->priority;
	old_max_q = rmp->max_priority;

	/* Update the proc entry and reschedule the process */
	rmp->max_priority = rmp->priority = new_q;

	if ((rv = rmp->schedule_process(SCHEDULE_CHANGE_PRIO | SCHEDULE_CHANGE_QUANTUM)) != OK) 
	{
		rmp->priority     = old_q;
		rmp->max_priority = old_max_q;
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

extern "C" int Schedproc::do_start_scheduling(int proc_nr_n)
{
	Schedproc *rmp;
	int rv;
	
	assert(dec.mtype == SCHEDULING_START || dec.mtype == SCHEDULING_INHERIT);
	rmp = &schedproc[proc_nr_n];
	rmp->endpoint     = dec.endpoint;
	rmp->parent       = dec.parent;
	rmp->max_priority = (unsigned) dec.maxprio;
	rmp->burst_hist_cnt = 0;
	if (rmp->max_priority >= NR_SCHED_QUEUES) {
		return EINVAL;
	}
	if (rmp->endpoint == rmp->parent) {
		rmp->priority   = USER_Q;
		rmp->time_slice = DEFAULT_USER_TIME_SLICE;
#ifdef CONFIG_SMP
		rmp->cpu = machine.bsp_id;
#endif
	}
	switch (dec.mtype) {
	case SCHEDULING_START:
		rmp->priority   = rmp->max_priority;
		rmp->time_slice = (unsigned) dec.quantum;
		rmp->base_time_slice = rmp->time_slice;
		break;		
	case SCHEDULING_INHERIT:
		rmp->priority = dec.parent_priority;
		rmp->time_slice = dec.parent_time_slice;
		rmp->base_time_slice = rmp->time_slice;
		break;		
	default: 
		assert(0);
	}
	if ((rv = call_minix_sys_schedctl(0, rmp->endpoint, 0, 0, 0)) != OK) {
		printf("Sched: Error taking over scheduling for %d, kernel said %d\n",rmp->endpoint, rv);
		return rv;
	}
	rmp->flags = IN_USE;
	rmp->pick_cpu();
	while ((rv = rmp->schedule_process(SCHEDULE_CHANGE_ALL)) == EBADCPU) {
		cpu_proc[rmp->cpu] = CPU_DEAD;
		rmp->pick_cpu();
	}
	if (rv != OK) {
		printf("Sched: Error while scheduling process, kernel replied %d\n",rv);
		return rv;
	}
	//m_ptr->SCHEDULING_SCHEDULER = SCHED_PROC_NR;
	return OK;
}

/*===========================================================================*
 *				sched_isokendpt			 	     *
 *===========================================================================*/
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

/*===========================================================================*
 *				sched_isemtyendpt		 	     *
 *===========================================================================*/
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

/*===========================================================================*
 *				accept_message				     *
 *===========================================================================*/
int accept_message(message *m_ptr)
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
			dec.parent_priority   = schedproc[parent_nr_n].priority;
			dec.parent_time_slice = schedproc[parent_nr_n].time_slice;
		}
		m_ptr->SCHEDULING_SCHEDULER = SCHED_PROC_NR;
	}
	else {
		if (req != SCHEDULING_NO_QUANTUM) {
			if (sched_isokendpt(m_ptr->SCHEDULING_ENDPOINT, &proc_nr_n) != OK) {
				printf("SCHED: WARNING: got an invalid endpoint in OOQ msg ""%ld\n", m_ptr->SCHEDULING_ENDPOINT);
				return EBADEPT;
			}
		}
	}

	dec.endpoint = m_ptr->SCHEDULING_ENDPOINT;
	dec.parent = m_ptr->SCHEDULING_PARENT;
	dec.quantum = m_ptr->SCHEDULING_QUANTUM;
	dec.maxprio = m_ptr->SCHEDULING_MAXPRIO;
	dec.acnt_ipc_async = m_ptr->SCHEDULING_ACNT_IPC_ASYNC;
	dec.acnt_cpu_load = m_ptr->SCHEDULING_ACNT_CPU_LOAD;
	dec.mtype = m_ptr->m_type;
	dec.p = &m_ptr;	// salvo o endereco da msg, depois isso aqui resolver "TUDO"

	return proc_nr_n;
}

extern "C" int no_sys(int who_e, int call_nr)
{
/* A system call number not implemented by PM has been requested. */
  printf("SCHED: in no_sys, call nr %d from %d\n", call_nr, who_e);
  return(ENOSYS);
}

extern "C" int invoke_sched_method(int index, int function)
{
	Schedproc *rmp;
	rmp = &schedproc[index];
	
	switch(function){
		case SCHEDULING_START:
			return rmp->do_start_scheduling(index);
		case SCHEDULING_STOP:
			return rmp->do_stop_scheduling(index);
		case SCHEDULING_SET_NICE:
			return rmp->do_nice(index);
		case SCHEDULING_NO_QUANTUM:
			return rmp->do_noquantum(index);
	}
	return 0;	
}
