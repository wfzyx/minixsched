#include "sched.h"
#include "schedproc.h"
#include <assert.h>



#include "sched.h"
#include <machine/archtypes.h>
#include <sys/resource.h> /* for PRIO_MAX & PRIO_MIN */
#include "kernel/proc.h" /* for queue constants */
#include "schedproc.h"





#define SCHEDULE_CHANGE_PRIO	0x1
#define SCHEDULE_CHANGE_QUANTUM	0x2
#define SCHEDULE_CHANGE_CPU	0x4

#define SCHEDULE_CHANGE_ALL	(	\
		SCHEDULE_CHANGE_PRIO	|	\
		SCHEDULE_CHANGE_QUANTUM	|	\
		SCHEDULE_CHANGE_CPU		\
		)


// FAZER OVERLOADS
#define schedule_process_local(p)	\
	schedule_process(p, SCHEDULE_CHANGE_PRIO | SCHEDULE_CHANGE_QUANTUM)
#define schedule_process_migrate(p)	\
	schedule_process(p, SCHEDULE_CHANGE_CPU)

#define CPU_DEAD	-1

#define DEFAULT_USER_TIME_SLICE 200
#define INC_PER_QUEUE 10

unsigned cpu_proc[CONFIG_MAX_CPUS];

#define cpu_is_available(c)	(cpu_proc[c] >= 0)
#define is_system_proc(p)	((p)->parent == RS_PROC_NR)

extern "C" int sys_schedule(endpoint_t proc_ep, int priority, int quantum, int cpu);

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


//void Schedproc::setValues (Schedroc const &src)
//{
//	endpoint = src.endpoint;
//	parent = src.parent;
//	flags = src.flags;
//	max_priority = src.max_priority;
//	priority = src.priority;
//	base_time_slice = src.base_time_slice;
//	time_slice = src.time_slice;
//	cpu = src.cpu;
//	cpu_mask[BITMAP_CHUNKS = 
//src.cpu_mask[BITMAP_CHUNKS(CONFIG_MAX_CPUS)];
//	burst_history[BURST_HISTORY_LENGTH] = 
//src.burst_history[BURST_HISTORY_LENGTH];
//	burst_hist_cnt = src.burst_hist_cnt;
//}

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

int Schedproc::do_noquantum(message *m_ptr) {
	int rv, proc_nr_n;
	unsigned ipc, burst, queue_bump;
	short load;

	if (this->sched_isokendpt(m_ptr->m_source, &proc_nr_n) != OK) {
		printf("SCHED: WARNING: got an invalid endpoint in OOQ msg %u.\n",m_ptr->m_source);
		return EBADEPT;
	}

	// TODO : ROTINA DE COPIA
	//rmp = &schedproc[proc_nr_n];

	ipc = (unsigned)m_ptr->SCHEDULING_ACNT_IPC_ASYNC + (unsigned)m_ptr->SCHEDULING_ACNT_IPC_SYNC + 1;

	load = m_ptr->SCHEDULING_ACNT_CPU_LOAD;
	
	burst = (this->time_slice * 1000 / ipc) / 100;
	
	burst = this->burst_smooth(burst);

	queue_bump = burst/INC_PER_QUEUE;

	if (this->max_priority + queue_bump > MIN_USER_Q) {
		queue_bump = MIN_USER_Q - this->max_priority;
	}

	this->priority = this->max_priority + queue_bump;
	this->time_slice = this->base_time_slice + 2 * queue_bump * (this->base_time_slice/10);

	// TODO CHECK WHERE IN HELL THE FLAG SHOULD COME FROM
        unsigned placeholder = 0;
	if ((rv = this->schedule_process(placeholder)) != OK) {
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

int Schedproc::do_stop_scheduling(message *m_ptr)
{
	int proc_nr_n;

	/* check who can send you requests */
	if (!this->accept_message(m_ptr))
		return EPERM;

	if (this->sched_isokendpt(m_ptr->SCHEDULING_ENDPOINT, &proc_nr_n) != OK) {
		printf("SCHED: WARNING: got an invalid endpoint in OOQ msg "
		"%ld\n", m_ptr->SCHEDULING_ENDPOINT);
		return EBADEPT;
	}

    // TODO : COPY ROUTINE
	//this = schedproc[proc_nr_n];
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
	if(endpoint != schedproc[*proc]->endpoint)
		return (EDEADEPT);
	if(!(schedproc[*proc]->flags & IN_USE))
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
	if(schedproc[*proc]->flags & IN_USE)
		return (EDEADEPT);
	return (OK);
}


int Schedproc::do_nice(message *m_ptr)
{
	int rv;
	int proc_nr_n;
	unsigned new_q, old_q, old_max_q;

	/* check who can send you requests */
	if (!this->accept_message(m_ptr))
		return EPERM;

	if (this->sched_isokendpt(m_ptr->SCHEDULING_ENDPOINT, &proc_nr_n) != OK) {
		printf("SCHED: WARNING: got an invalid endpoint in OOQ msg ""%ld\n", m_ptr->SCHEDULING_ENDPOINT);
		return EBADEPT;
	}

	// TODO : ROTINA DE COPIA
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
		cout << "PM: An error occurred when trying to schedule " << this->endpoint << ": " << err;
	}

	return err;
}

int Schedproc::do_start_scheduling(message *m_ptr)
{
	int rv, proc_nr_n, parent_nr_n;
	
	/* we can handle two kinds of messages here */
	assert(m_ptr->m_type == SCHEDULING_START || 
		m_ptr->m_type == SCHEDULING_INHERIT);

	/* check who can send you requests */
	if (!this->accept_message(m_ptr))
		return EPERM;

	/* Resolve endpoint to proc slot. */
	if ((rv = this->sched_isemtyendpt(m_ptr->SCHEDULING_ENDPOINT, &proc_nr_n))
			!= OK) {
		return rv;
	}

// TODO IMPLEMENT COPY()
//	this = &schedproc[proc_nr_n];

	/* Populate process slot */
	this->endpoint     = m_ptr->SCHEDULING_ENDPOINT;
	this->parent       = m_ptr->SCHEDULING_PARENT;
	this->max_priority = (unsigned) m_ptr->SCHEDULING_MAXPRIO;
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
	
	switch (m_ptr->m_type) {

	case SCHEDULING_START:
		/* We have a special case here for system processes, for which
		 * quanum and priority are set explicitly rather than inherited 
		 * from the parent */
		this->priority   = this->max_priority;
		this->time_slice = (unsigned) m_ptr->SCHEDULING_QUANTUM;
		this->base_time_slice = this->time_slice;
		break;
		
	case SCHEDULING_INHERIT:
		/* Inherit current priority and time slice from parent. Since there
		 * is currently only one scheduler scheduling the whole system, this
		 * value is local and we assert that the parent endpoint is valid */
		if ((rv = this->sched_isokendpt(m_ptr->SCHEDULING_PARENT,
				&parent_nr_n)) != OK)
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
	//TODO CHECK EXTERN C
	if ((rv = sys_schedctl(0, this->endpoint, 0, 0, 0)) != OK) {
		printf("Sched: Error taking over scheduling for %d, kernel said %d\n",
			this->endpoint, rv);
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
		printf("Sched: Error while scheduling process, kernel replied %d\n",
			rv);
		return rv;
	}

	/* Mark ourselves as the new scheduler.
	 * By default, processes are scheduled by the parents scheduler. In case
	 * this scheduler would want to delegate scheduling to another
	 * scheduler, it could do so and then write the endpoint of that
	 * scheduler into SCHEDULING_SCHEDULER
	 */

	m_ptr->SCHEDULING_SCHEDULER = SCHED_PROC_NR;

	return OK;
}

int Schedproc::no_sys(int who_e, int call_nr)
{
/* A system call number not implemented by PM has been requested. */
  printf("SCHED: in no_sys, call nr %d from %d\n", call_nr, who_e);
  return(ENOSYS);
}


int Schedproc::accept_message(message *m_ptr)
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
