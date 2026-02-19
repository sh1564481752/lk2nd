/*
 * Copyright (c) 2008 Travis Geiselbrecht
 *
 * Copyright (c) 2009-2014, The Linux Foundation. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include <compiler.h>
#include <debug.h>
#include <string.h>
#include <app.h>
#include <arch.h>
#include <platform.h>
#include <target.h>
#include <lib/heap.h>
#include <kernel/thread.h>
#include <kernel/timer.h>
#include <kernel/dpc.h>
#include <boot_stats.h>

#if WITH_LIB_BIO
#include <lib/bio.h>
#endif
#if WITH_LIB_FS
#include <lib/fs.h>
#endif

extern void *__ctor_list;
extern void *__ctor_end;
extern int __bss_start;
extern int _end;

static int bootstrap2(void *arg);

#if (ENABLE_NANDWRITE)
void bootstrap_nandwrite(void);
#endif

static void call_constructors(void)
{
	void **ctor;

	ctor = &__ctor_list;
	while (ctor != &__ctor_end)
	{
		void (*func)(void);

		func = (void (*)(void))*ctor;

		func();
		ctor++;
	}
}

uintptr_t __stack_chk_guard;

/* called from crt0.S */
void kmain(void) __NO_RETURN __EXTERNALLY_VISIBLE;
/**
 * @brief 内核主函数，负责系统初始化和启动。
 *
 * 该函数是系统启动的核心入口点，执行一系列初始化操作，包括线程、堆、定时器等子系统的初始化，
 * 并最终创建引导完成线程或进入空闲线程状态。
 */
void kmain(void)
{
	thread_t *thr;

	// 初始化线程上下文，确保系统处于有效的线程环境中
	thread_init_early();

	// 执行架构相关的早期初始化操作
	arch_early_init();

	// 执行平台相关的超早期初始化操作
	platform_early_init();

	// 执行目标平台相关的超早期初始化操作
	target_early_init();

	// 初始化调试系统
	debug_init();
	dprintf(INFO, "welcome to lk\n\n");
	bs_set_timestamp(BS_BL_START);

	// 调用静态构造函数，完成全局对象的构造
	dprintf(SPEW, "calling constructors\n");
	call_constructors();

	// 初始化内核堆管理器
	dprintf(SPEW, "initializing heap\n");
	heap_init();

	// 设置栈保护机制
	__stack_chk_guard_setup();

	// 初始化线程管理系统
	dprintf(SPEW, "initializing threads\n");
	thread_init();

	// 初始化延迟过程调用（DPC）系统
	dprintf(SPEW, "initializing dpc\n");
	dpc_init();

	// 初始化内核定时器系统
	dprintf(SPEW, "initializing timers\n");
	timer_init();

#if (!ENABLE_NANDWRITE)
	// 创建引导完成线程，用于后续系统初始化工作
	dprintf(SPEW, "creating bootstrap completion thread\n");
	thr = thread_create("bootstrap2", &bootstrap2, NULL, DEFAULT_PRIORITY, DEFAULT_STACK_SIZE);
	if (!thr)
	{
		panic("failed to create thread bootstrap2\n");
	}
	thread_resume(thr);

	// 启用中断，允许系统响应外部事件
	exit_critical_section();

	// 将当前线程转换为空闲线程，等待任务调度
	thread_become_idle();
#else
	// 在NAND写入模式下，直接执行引导写入逻辑
	bootstrap_nandwrite();
#endif
}

int main(void);

/**
 * @brief 第二阶段引导函数，负责初始化系统各个组件。
 *
 * 该函数是系统启动过程中的第二阶段，主要完成架构、平台、目标设备以及应用程序的初始化工作。
 * 它依次调用架构初始化、平台初始化、目标设备初始化和应用程序初始化函数，确保系统各模块按顺序正确启动。
 *
 * @param arg 传递给函数的参数，当前未使用。
 * @return 返回值始终为0，表示初始化成功。
 */
static int bootstrap2(void *arg)
{
	dprintf(SPEW, "top of bootstrap2()\n");

	// 初始化系统架构相关组件
	arch_init();

	// XXX put this somewhere else
#if WITH_LIB_BIO
	// 初始化块I/O库（如果启用）
	bio_init();
#endif
#if WITH_LIB_FS
	// 初始化文件系统库（如果启用）
	fs_init();
#endif

	// 初始化平台相关组件
	dprintf(SPEW, "initializing platform\n");
	platform_init();

	// 初始化目标设备相关组件
	dprintf(SPEW, "initializing target\n");
	target_init();

	// 调用应用程序初始化函数
	dprintf(SPEW, "calling apps_init()\n");
	apps_init();

	return 0;
}

#if (ENABLE_NANDWRITE)
void bootstrap_nandwrite(void)
{
	dprintf(SPEW, "top of bootstrap2()\n");

	arch_init();

	// initialize the rest of the platform
	dprintf(SPEW, "initializing platform\n");
	platform_init();

	// initialize the target
	dprintf(SPEW, "initializing target\n");
	target_init();

	dprintf(SPEW, "calling nandwrite_init()\n");
	nandwrite_init();

	return 0;
}
#endif
