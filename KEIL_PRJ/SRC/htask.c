#include "htask.h"
#include "hlist.h"
#include "hbitmap.h"

ScheduleStateType hTaskScheduleState;

hBitmap hTaskBitmap;

hList hTaskReadyTable[32];
hList hTaskBlockedList;
hList hTaskSuspendList;
hList hTaskDelayList;

hTask * current_hTask_t;
hTask * next_hTask_t;

hTaskStack hTaskIdle_env[64];
hTask hTaskIdle;

void ScheduleStateInit()
{ 
    hTaskScheduleState = ScheduleEnable;
}

hTask * hTaskGetMaxPrio()
{
    uint32_t max_prio_pos = hBitmapGetFirstSet(&hTaskBitmap);
    hNode* node_t = hTaskReadyTable[max_prio_pos].hNode_head;
    return hNodeParent(node_t, hTask, linkNode);
}

void hTaskList_init()
{
    uint16_t i;
    for (i = 0; i < 32; i++) hList_init(&hTaskReadyTable[i]);
    hList_init(&hTaskBlockedList); 
    hList_init(&hTaskSuspendList);
}

hTaskErrorType hTask_init(hTask * hTask_t,char * task_name,void (* func_entry)(void *),void * param,uint32_t priority, hTaskStack * stack)
{
    *(--stack) = (uint32_t)(1<<24); // xPSR
    *(--stack) = (uint32_t)func_entry; // PC
    *(--stack) = (uint32_t)0x14; // R14(LR)
    *(--stack) = (uint32_t)0x12; // R12
    *(--stack) = (uint32_t)0x03; // R3
    *(--stack) = (uint32_t)0x02; // R2
    *(--stack) = (uint32_t)0x01; // R1
    *(--stack) = (uint32_t)param; // R0

    // R11 - R4
    *(--stack) = (uint32_t)0x11; 
    *(--stack) = (uint32_t)0x10; 
    *(--stack) = (uint32_t)0x09; 
    *(--stack) = (uint32_t)0x08;
    *(--stack) = (uint32_t)0x07; 
    *(--stack) = (uint32_t)0x06; 
    *(--stack) = (uint32_t)0x05; 
    *(--stack) = (uint32_t)0x04;

    hTask_t->stack = stack;
    hTask_t->priority = priority;
    hTask_t->delay_ticks = 0;
    hTask_t->time_slice = TIME_SLICE_CNT;
    hTask_t->state = TASK_READY;
    hNode_init(&(hTask_t->linkNode));
    hListAddFirst(&hTaskReadyTable[hTask_t->priority],&(hTask_t->linkNode));
    hBitmapSet(&hTaskBitmap,hTask_t->priority);
		
		return OKAY;
}

void tTaskSystemTickHandler(void)
{
    if( hTaskScheduleState != ScheduleEnable )
    {
        return;
    }

    if(--current_hTask_t->time_slice == 0) // time slice turn run
    {
        current_hTask_t->time_slice = TIME_SLICE_CNT;
        hListRunCircle(&hTaskReadyTable[current_hTask_t->priority]);
        current_hTask_t->state = TASK_READY;
    }

    for(hNode* node_t = hTaskDelaydList.hNode_head; node_t->next != hTaskDelaydList.hNode_head; node_t=node_t->next)
    {
        hTask * hTask_t = hNodeParent(node_t, hTask, linkNode);
        if( --(hTask_t->delay_ticks) == 0)
        {
            hListRemove(&hTaskDelayList,&(hTask_t->linkNode));
            hListAddFirst(&hTaskReadyTable[hTask_t->priority],&(hTask_t->linkNode));
            hTask_t->state = TASK_READY;
        }
    }
    
    hTaskSchedule();
}

void hTaskSchedule(void)
{
    if( hTaskScheduleState != ScheduleEnable )
    {
        return;
    }
    
    hTask* hTaskPrio_t;
    hTaskPrio_t = hTaskGetMaxPrio();

    if( current_hTask_t != hTaskPrio_t)
    {  
        next_hTask_t = hTaskPrio_t;
        next_hTask_t->state = TASK_RUNNING;
        hTaskSwitch();
    }
}


void hTaskChoke(hTask * hTask_t,hTaskChokeType choke_type)
{
    ScheduleStateType local_tmp;
    Schedule_Enter_Critical(hTaskScheduleState,local_tmp);
    if(hTask_t->state != TASK_RUNNING)
    {
        Schedule_Exit_Critical(hTaskScheduleState,local_tmp);
        return;
    }
    hTask_t->state = TASK_BLOCKED;
    if (choke_type == CHOKE_DELAY) //CHOKE_DELAY
    {
        hListAddFirst(&hTaskDelayList,&(hTask_t->linkNode)); 
    }
    else  //CHOKE_EVENT
    {
        hListAddFirst(&hTaskBlockedList,&(hTask_t->linkNode));
    }
    hListRemove(&hTaskReadyTable[hTask_t->priority],&(hTask_t->linkNode));
    if ( hTaskReadyTable[(hTask_t->priority)].hNode_head == (void *)0 )
    {
        hBitmapClear(&hTaskBitmap,hTask_t->priority);
    }
    Schedule_Exit_Critical(hTaskScheduleState,local_tmp);
    hTaskSchedule();
}


void hTaskWakeUp(hTask * hTask_t)
{
    ScheduleStateType local_tmp;
    Schedule_Enter_Critical(hTaskScheduleState,local_tmp);
    if( hTask_t->state != TASK_BLOCKED )
    {
        Schedule_Exit_Critical(hTaskScheduleState,local_tmp);
        return;        
    }
    hTask_t->state = TASK_READY;
    hListAddFirst(&hTaskReadyTable[hTask_t->priority],&(hTask_t->linkNode));
    hListRemove(&hTaskBlockedList,&(hTask_t->linkNode));
    hBitmapSet(&hTaskBitmap,hTask_t->priority);
    Schedule_Exit_Critical(hTaskScheduleState,local_tmp);
    hTaskSchedule();
}


void hTaskSuspend(hTask * hTask_t)
{
    ScheduleStateType local_tmp;
    Schedule_Enter_Critical(hTaskScheduleState,local_tmp);
    if(hTask_t->state != TASK_RUNNING && hTask_t->state != TASK_READY)
    {
        Schedule_Exit_Critical(hTaskScheduleState,local_tmp);
        return;
    }
    hTask_t->state = TASK_SUSPEND;
    hListAddFirst(&hTaskSuspendList,&(hTask_t->linkNode));
    hListRemove(&hTaskReadyTable[hTask_t->priority],&(hTask_t->linkNode));
    if ( hTaskReadyTable[(hTask_t->priority)].hNode_head == (void *)0 )
    {
        hBitmapClear(&hTaskBitmap,hTask_t->priority);
    }
    Schedule_Exit_Critical(hTaskScheduleState,local_tmp);
    hTaskSchedule();
}


void hTaskResume(hTask * hTask_t)
{
    ScheduleStateType local_tmp;
    Schedule_Enter_Critical(hTaskScheduleState,local_tmp);
    if( hTask_t->state != TASK_SUSPEND )
    {
        Schedule_Exit_Critical(hTaskScheduleState,local_tmp);
        return;
    }
    hTask_t->state = TASK_READY;
    hListAddFirst(&hTaskReadyTable[hTask_t->priority],&(hTask_t->linkNode));
    hBitmapSet(&hTaskBitmap,hTask_t->priority);
    hListRemove(&hTaskSuspendList,&(hTask_t->linkNode));
    Schedule_Exit_Critical(hTaskScheduleState,local_tmp);
    hTaskSchedule();
}


void hTaskDelay(uint32_t ticks)
{
    ScheduleStateType local_tmp;
    Schedule_Enter_Critical(hTaskScheduleState,local_tmp);
    if(hTask_t->state != TASK_RUNNING)
    {
        Schedule_Exit_Critical(hTaskScheduleState,local_tmp);
        return;
    }
    current_hTask_t->delay_ticks = ticks;
    hTaskChoke(current_hTask_t,CHOKE_DELAY); //这里只会执行 hTaskChoke 里的逻辑，但是hTaskChoke里面的调度函数不会执行
    Schedule_Exit_Critical(hTaskScheduleState,local_tmp);
    hTaskSchedule(); //上面hTaskChoke里面的调度函数不会执行，这里必须补上才能切换任务
}

void hTask_Param_init(void)
{
    hBitmap_init(&hTaskBitmap);
    hTaskList_init();
    hTaskErrorType hTaskType = hTask_init(&hTaskIdle,\
                                        "Idle Task",\
                                        hTaskIdle_thread,\
                                        (void *) 0, \
                                        31 , \
                                        &hTaskIdle_env[64]);


}

void hTaskIdle_thread(void * param)
{

}


void hTaskRunFirst (void)
{
	__set_PSP(0);
	MEM8(NVIC_SYSPRI2) = NVIC_PENDSV_PRI;
	MEM32(NVIC_INT_CTRL) = NVIC_PENDSVSET;
}
