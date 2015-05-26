#include <limits.h>
#include <minix/bitmap.h>
#include "sched.h"
//#include "schedproc.h"

#ifndef CONFIG_SMP
#define CONFIG_MAX_CPUS 1
#endif

#define BURST_HISTORY_LENGTH 10

// somente define a classe se o include for chamado no C++
#ifdef __cplusplus
 class Schedproc2 {
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
  //public:
  //  Schedproc();
  //  Schedproc (struct schedproc argStruct);
  //  struct schedproc toStruct ();

 };
// A ling. C nao reconhece classes, somente dizemos que eh uma estrutura
#else
 typedef struct Schedproc2 Schedproc2;
#endif

// funcoes de acesso
#ifdef __cplusplus
    #define EXPORT_C extern "C"
#else
    #define EXPORT_C
#endif

//EXPORT_C Schedproc* Schedproc_new(void);
//EXPORT_C void Schedproc_delete(Schedproc* this);
//EXPORT_C Schedproc (Schedproc*, struct schedproc argStruct);

/*
struct schedproc Schedproc::toStruct(){

	struct schedproc retStruct;
	retStruct.endpoint = endpoint;
	retStruct.parent = parent;
	retStruct.flags = flags;
	retStruct.max_priority = max_priority;
	retStruct.priority = priority;
	retStruct.base_time_slice = base_time_slice;
	retStruct.time_slice = time_slice;
	retStruct.cpu = cpu;
	//retStruct.cpu_mask[BITMAP_CHUNKS] = cpu_mask[BITMAP_CHUNKS(CONFIG_MAX_CPUS)];
	retStruct.burst_history[BURST_HISTORY_LENGTH] = burst_history[BURST_HISTORY_LENGTH];
	retStruct.burst_hist_cnt = burst_hist_cnt;
	return retStruct;
}


Schedproc::Schedproc (struct schedproc argStruct) {

	endpoint = argStruct.endpoint;
	parent = argStruct.parent;
	flags = argStruct.flags;
	max_priority = argStruct.max_priority;
	priority = argStruct.priority;
	base_time_slice = argStruct.base_time_slice;
	time_slice = argStruct.time_slice;
	cpu = argStruct.cpu;
	//cpu_mask[BITMAP_CHUNKS = argStruct.cpu_mask[BITMAP_CHUNKS(CONFIG_MAX_CPUS)];
	burst_history[BURST_HISTORY_LENGTH] = argStruct.burst_history[BURST_HISTORY_LENGTH];
	burst_hist_cnt = argStruct.burst_hist_cnt;

}
*/
