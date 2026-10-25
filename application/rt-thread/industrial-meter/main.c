/*
 * 便携工业调试仪 —— 主机应用入口
 */

#include <rtthread.h>
#ifdef RT_USING_ULOG
#include <ulog.h>
#endif

#include "im_master.h"
#include "im_d12x_report.h"

int main(void)
{
#ifdef ULOG_USING_FILTER
    ulog_global_filter_lvl_set(ULOG_OUTPUT_LVL);
#endif

    /* 启动 RS485 主站轮询，之后用 im_dump / im_rd / im_wr 调试 */
    im_master_start();

    /* D12x 把自身运行态 (结温/电量/CPU/内存/运行时间) 写回从站 0x0400~0x0404，
     * 由星火一号中转上云。须在 master 启动之后调用。 */
    im_d12x_report_start();

    return 0;
}
