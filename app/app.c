/*
 * Copyright (c) 2009 Travis Geiselbrecht
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
#include <debug.h>
#include <app.h>
#include <kernel/thread.h>

extern const struct app_descriptor __apps_start;
extern const struct app_descriptor __apps_end;

static void start_app(const struct app_descriptor *app);

/* one time setup */
/**
 * apps_init - 初始化并启动应用程序
 *
 * 此函数负责初始化系统中所有注册的应用程序，并根据配置决定是否在启动时自动运行它们。
 * 它遍历应用程序描述符数组，依次调用每个应用程序的初始化函数，然后启动那些未被标记为禁止启动的应用程序。
 */
void apps_init(void)
{
	const struct app_descriptor *app;

	/* 遍历所有应用程序描述符，调用每个应用程序的初始化函数 */
	for (app = &__apps_start; app != &__apps_end; app++)
	{
		if (app->init)
			app->init(app);
	}

	/* 遍历所有应用程序描述符，启动那些未被标记为禁止启动的应用程序 */
	for (app = &__apps_start; app != &__apps_end; app++)
	{
		if (app->entry && (app->flags & APP_FLAG_DONT_START_ON_BOOT) == 0)
		{
			start_app(app);
		}
	}
}

/**
 * @brief 应用线程的入口函数。
 *
 * 该函数作为线程的执行入口，接收一个指向应用描述符的指针作为参数，
 * 并调用该应用描述符中定义的入口函数来启动应用程序。
 *
 * @param arg 指向应用描述符的指针，类型为 void*，实际类型为 const struct app_descriptor*。
 *            该描述符包含了应用程序的相关信息，包括入口函数指针。
 *
 * @return 返回值始终为 0，表示线程正常退出。
 */
static int app_thread_entry(void *arg)
{
	// 将传入的参数转换为应用描述符指针
	const struct app_descriptor *app = (const struct app_descriptor *)arg;

	// 调用应用描述符中定义的入口函数，启动应用程序
	// 第二个参数为 NULL，表示不传递额外的上下文信息
	app->entry(app, NULL);

	// 线程执行完毕，返回 0 表示正常退出
	return 0;
}

/**
 * 启动指定的应用程序。
 *
 * 该函数根据传入的应用描述符创建并启动一个新线程来运行应用程序。
 * 线程创建成功后会立即恢复执行。
 *
 * @param app 指向应用程序描述符的指针，包含应用程序的名称和其他相关信息。
 */
static void start_app(const struct app_descriptor *app)
{
	thread_t *thr;

	// 打印启动应用程序的日志信息
	printf("starting app %s\n", app->name);

	// 创建新线程以运行应用程序入口函数
	thr = thread_create(app->name, &app_thread_entry, (void *)app, DEFAULT_PRIORITY, DEFAULT_STACK_SIZE);
	if (!thr)
	{
		// 如果线程创建失败，则直接返回
		return;
	}

	// 恢复新创建的线程以开始执行
	thread_resume(thr);
}
