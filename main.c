/* This file contains the main program of the SCHED scheduler. It will sit idle
 * until asked, by PM, to take over scheduling a particular process.
 */

/* The _MAIN def indicates that we want the schedproc structs to be created
 * here. Used from within schedproc.h */
#define _MAIN
 
#include "sched.h"
#include "schedproc.h"

/* Declare some local functions. */
static void reply(endpoint_t whom, message *m_ptr);
static void sef_local_startup(void);
int decoder(int req, message *m_ptr);
int sched_isokendpt(int endpoint, int *proc);
int sched_isemtyendpt(int endpoint, int *proc);
int accept_message(message *m_ptr);
static void init_scheduling(void);
struct machine machine;		/* machine info */
struct M;
int do_start_scheduling(message *m_ptr);
int do_stop_scheduling(message *m_ptr);
int do_nice(message *m_ptr);
int do_noquantum(message *m_ptr);
int no_sys(int who_e, int call_nr);	
/*===========================================================================*
 *				main					     *
 *===========================================================================*/
int main(void)
{
	/* Main routine of the scheduler. */
	message m_in;	/* the incoming message itself is kept here. */
	int call_nr;	/* system call number */
	int who_e;	/* caller's endpoint */
	int result;	/* result to system call */
	int rv;
	int s;

	/* SEF local startup. */
	sef_local_startup();

	if (OK != (s=sys_getmachine(&machine)))
		panic("couldn't get machine info: %d", s);
	/* Initialize scheduling timers, used for running balance_queues */
	init_scheduling();

	/* This is SCHED's main loop - get work and do it, forever and forever. */
	while (TRUE) {
		int ipc_status;

		/* Wait for the next message and extract useful information from it. */
		if (sef_receive_status(ANY, &m_in, &ipc_status) != OK)
			panic("SCHED sef_receive error");
		who_e = m_in.m_source;	/* who sent the message */
		call_nr = m_in.m_type;	/* system call number */

		/* Check for system notifications first. Special cases. */
		if (is_ipc_notify(ipc_status)) {
			switch(who_e) {
				case CLOCK:
					expire_timers(m_in.NOTIFY_TIMESTAMP);
					continue;	/* don't reply */
				default :
					result = ENOSYS;
			}

			goto sendreply;
		}
		
		// calling the interface between SP and OO
		proc_num = decoder(call_nr, &m_in);

		switch(call_nr) {
		case SCHEDULING_INHERIT:
		case SCHEDULING_START:
			//result = Schedproc::do_start_scheduling(proc_num);
			break;
		case SCHEDULING_STOP:
			result = Schedproc::do_stop_scheduling(proc_num);
			break;
		case SCHEDULING_SET_NICE:
			result = Schedproc::do_nice(proc_num);
			break;
		case SCHEDULING_NO_QUANTUM:
			/* This message was sent from the kernel, don't reply */
			if (IPC_STATUS_FLAGS_TEST(ipc_status,IPC_FLG_MSG_FROM_KERNEL)) {
				if ((rv = Schedproc::do_noquantum(proc_num)) != (OK)) {
					printf("SCHED: Warning, do_noquantum failed with %d\n", rv);
				}
				continue; /* Don't reply */
			}
			else {
				printf("SCHED: process %d faked SCHEDULING_NO_QUANTUM message!\n",who_e);
				result = EPERM;
			}
			break;
		default:
			result = no_sys(who_e, call_nr);
		}

sendreply:
		/* Send reply. */
		if (result != SUSPEND) {
			m_in.m_type = result;  		/* build reply message */
			reply(who_e, &m_in);		/* send it away */
		}
 	}
	return(OK);
}

/*===========================================================================*
 *				reply					     *
 *===========================================================================*/
static void reply(endpoint_t who_e, message *m_ptr)
{
	int s = send(who_e, m_ptr);    /* send the message */
	if (OK != s)
		printf("SCHED: unable to send reply to %d: %d\n", who_e, s);
}

/*===========================================================================*
 *			       sef_local_startup			     *
 *===========================================================================*/
static void sef_local_startup(void)
{
	/* No init callbacks for now. */
	/* No live update support for now. */
	/* No signal callbacks for now. */

	/* Let SEF perform startup. */
	sef_startup();
}

/*===========================================================================*
 *			       decoder					     *
 *===========================================================================*/
int decoder(int req, message *m_ptr) 
{
	int rv, proc_nr_n;

	if (req != SCHEDULING_NO_QUANTUM) {
		if (!accept_message(m_ptr))
			return EPERM;
	}

	if (req == SCHEDULING_INHERIT) || (req == SCHEDULING_START) {
		if ((rv = sched_isemtyendpt(m_ptr->SCHEDULING_ENDPOINT, &proc_nr_n)) != OK) {
			return rv;
		}
	}
	else {
		if (req != SCHEDULING_NO_QUANTUM) {
			if (sched_isokendpt(m_ptr->SCHEDULING_ENDPOINT, &proc_nr_n) != OK) {
				printf("SCHED: WARNING: got an invalid endpoint in OOQ msg ""%ld\n", m_ptr->SCHEDULING_ENDPOINT);
				return EBADEPT;
			}
		}
	}

	// futura estrutura temporaria do decoder
	// dec.maxprio = m_ptr->SCHEDULING_MAXPRIO;			// ?
	// dec.acnt_ipc_async = m_ptr->SCHEDULING_ACNT_IPC_ASYNC;	// unsigned
	// dec.acnt_cpu_load = m_ptr->SCHEDULING_ACNT_CPU_LOAD;		// ?

	return proc_nr_n;
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

void init_scheduling(void)
{
}

int call_minix_sys_schedule(endpoint_t proc_ep, int priority, int quantum, int cpu)
{
	return sys_schedule(proc_ep,priority,quantum,cpu);
}

int call_minix_sys_schedctl(unsigned flags, endpoint_t proc_ep, int priority, int quantum, int cpu)
{
	return sys_schedctl(flags,proc_ep,priority,quantum,cpu);
}
