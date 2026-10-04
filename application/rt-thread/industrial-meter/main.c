/*
 * 便携工业调试仪 —— 主机应用入口
 */

#include <rtthread.h>
#ifdef RT_USING_ULOG
#include <ulog.h>
#endif

#include "im_master.h"

int main(void)
{
#ifdef ULOG_USING_FILTER
    ulog_global_filter_lvl_set(ULOG_OUTPUT_LVL);
#endif

    /* 启动 RS485 主站轮询，之后用 im_dump / im_rd / im_wr 调试 */
    im_master_start();

    return 0;
}
