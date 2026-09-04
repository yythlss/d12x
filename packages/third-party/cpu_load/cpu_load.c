/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "cpu_load.h"

#ifdef CPU_LOAD_MONITOR

#define CPU_LOAD_MONITOR_SIZE 40

static task_monitor_t task_monitor_list[CPU_LOAD_MONITOR_SIZE] = {0};
static uint32_t task_count = 0;
static uint64_t monitor_base = 0;

extern int rt_thread_is_available(struct rt_thread *thread);

/* compact the monitor array by removing NULL slots, must be called under critical section */
static void task_monitor_compact(void)
{
    int w = 0;

    for (int r = 0; r < task_count; r++)
    {
        if (task_monitor_list[r].thread != NULL)
        {
            if (w != r)
                task_monitor_list[w] = task_monitor_list[r];
            w++;
        }
    }
    task_count = w;
}

void thread_init_hook(rt_thread_t thread)
{
    int free_slot = -1;
    int index;

    rt_enter_critical();
    for(index = 0; index < task_count; index++)
    {
        if(task_monitor_list[index].thread == thread)
        {
            /* already registered, reset counters */
            task_monitor_list[index].base = 0;
            rt_exit_critical();
            return;
        }
        if(task_monitor_list[index].thread == NULL && free_slot < 0)
        {
            free_slot = index;
        }
    }

    /* reuse a reclaimed NULL slot first */
    if(free_slot >= 0)
    {
        task_monitor_list[free_slot].thread = thread;
        task_monitor_list[free_slot].base = 0;
        task_monitor_list[free_slot].count = 0;
    }
    else if(task_count < CPU_LOAD_MONITOR_SIZE)
    {
        task_monitor_list[task_count].thread = thread;
        task_monitor_list[task_count].base = 0;
        task_monitor_list[task_count].count = 0;
        task_count++;
    }
    rt_exit_critical();
}

void sched_hook(struct rt_thread* from, struct rt_thread* to)
{
    int index = 0;
    static uint64_t base = 0;
    rt_base_t level;

    if(!base)
    {
        base = aic_get_time_us64();
        monitor_base = base;
    }
    uint64_t time = aic_get_time_us64();

    level = rt_hw_interrupt_disable();
    for(index = 0; index < task_count; index++)
    {
        if(task_monitor_list[index].thread == from)
        {
            /* lazily detect exited thread to avoid use-after-free */
            if(from->stat == RT_THREAD_CLOSE)
            {
                task_monitor_list[index].thread = NULL;
                break;
            }
            if(task_monitor_list[index].base == 0)
            {
                task_monitor_list[index].base = time;
                task_monitor_list[index].count = time;
            }
            else
            {
                task_monitor_list[index].count += time - base;
            }
            break;
        }
    }
    rt_hw_interrupt_enable(level);
    base = time;
}

int cpu_load(int argc, const char **argv)
{
    (void)argc;
    (void)argv;

    uint16_t total = 0, value = 0, cnt = 0;

    uint64_t time = aic_get_time_us64() - monitor_base;

    printf("ID   %%CPU Task name\n");
    printf("--- ----- ----------------\n");
    for(int i = 0; i < task_count; i++) {
        if (!task_monitor_list[i].thread)
            continue;

        if (!rt_thread_is_available(task_monitor_list[i].thread)) {
            rt_enter_critical();
            task_monitor_list[i].thread = NULL;
            rt_exit_critical();
            continue;
        }

        cnt++;
        value = (task_monitor_list[i].count- task_monitor_list[i].base)*1000/time;
        printf("%-3d %2d.%d%% %-s\n", cnt,
               value / 10, value % 10, task_monitor_list[i].thread->name);
        if (strncmp("tidle", task_monitor_list[i].thread->name, 5) != 0)
            total += value;
    }

    if (total > 1000)
        total = 1000;
    printf("\t\tTotal: %d.%d%%\n", total / 10, total % 10);

    /* compress array: reclaim NULL slots */
    rt_enter_critical();
    task_monitor_compact();

    monitor_base = aic_get_time_us64();
    for(int i = 0; i < task_count; i++)
    {
        task_monitor_list[i].base = monitor_base;
        task_monitor_list[i].count = monitor_base;
    }
    rt_exit_critical();
    return 0;
}

MSH_CMD_EXPORT(cpu_load, CPU monitor);
MSH_CMD_EXPORT_ALIAS(cpu_load, top, CPU monitor);
#endif

