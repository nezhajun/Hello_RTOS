#ifndef __HTASK_H
#define __HTASK_H

#include "htask_core.h"

#define TIME_SLICE_CNT 10


void hTaskList_init();
hTaskErrorType hTask_init(hTask * hTask_t,char * task_name,void (* func_entry)(void *),void * param,uint32_t priority, hTaskStack * stack);
void SysTick_Handler ();
void hTaskSystemTick_Handler();
void hTaskSchedule();
void hTaskChoke(hTask * hTask_t);
void hTaskWakeUp(hTask * hTask_t);
void hTaskSuspend(hTask * hTask_t);
void hTaskResume(hTask * hTask_t);
void hTaskDelay(uint32_t ticks);
void hTask_Param_init();

void hTaskIdle_thread();

void hTaskRunFirst (void);

#endif // !__HTASK_H