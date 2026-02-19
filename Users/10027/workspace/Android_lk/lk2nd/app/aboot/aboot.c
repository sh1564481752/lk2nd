/**
 * @brief 初始化aboot模块，处理启动流程中的各种情况。
 *
 * 该函数负责初始化aboot模块，并根据设备状态、按键输入、重启模式等因素决定进入正常启动、恢复模式或fastboot模式。
 * 它还处理多槽位支持、显示初始化、序列号读取、以及安全启动相关的逻辑。
 *
 * @param app 指向应用程序描述符的指针，用于传递应用相关信息。
 */
void aboot_init(const struct app_descriptor *app)
{
	unsigned reboot_mode = 0; // 存储重启模式，用于判断设备重启的原因
	int boot_err_type = 0;	  // 存储启动错误类型，用于记录启动过程中遇到的错误
	int boot_slot = INVALID;  // 当前活动槽位索引，用于A/B分区启动系统

	/* 初始化看门狗以捕获早期LK崩溃 */
	// 看门狗定时器用于检测系统是否正常运行，如果系统卡死则自动重启
#if WDOG_SUPPORT
	msm_wdog_init(); // 初始化高通平台的看门狗模块
#endif

	/* 设置NV存储的页面大小信息 */
	// NV存储指的是非易失性存储，如eMMC或UFS存储设备
	if (target_is_emmc_boot())
	{
		// 如果是从eMMC启动，则获取eMMC的相关参数
		page_size = mmc_page_size();           // 获取eMMC页面大小
		page_mask = page_size - 1;             // 计算页面掩码，用于页面对齐
		mmc_blocksize = mmc_get_device_blocksize(); // 获取eMMC块大小
		mmc_blocksize_mask = mmc_blocksize - 1;     // 计算块大小掩码
	}
	else
	{
		// 如果不是从eMMC启动（如NAND闪存），则获取闪存页面大小
		page_size = flash_page_size();
		page_mask = page_size - 1;
	}
	// 断言检查内存基址和大小的有效性
	ASSERT((MEMBASE + MEMSIZE) > MEMBASE);

#if !ABOOT_STANDALONE
	// 读取设备信息和OEM解锁状态（非独立模式下）
	read_device_info(&device);         // 从存储中读取设备信息结构体
	read_allow_oem_unlock(&device);    // 读取是否允许OEM解锁的标志
#else
	// 独立模式下，默认设备为已解锁状态
	device.is_unlocked = true;
#endif

	/* 检测多槽位支持 */
	// 多槽位支持用于A/B无缝更新系统
	if (partition_multislot_is_supported())
	{
		boot_slot = partition_find_active_slot(); // 查找当前活动的启动槽位
		if (boot_slot == INVALID)
		{
			// 如果没有找到有效槽位，则进入fastboot模式
			boot_into_fastboot = true;
			dprintf(INFO, "Active Slot: (INVALID)\n");
		}
		else
		{
			/* 将系统状态设置为从活动槽位启动 */
			partition_mark_active_slot(boot_slot); // 标记当前槽位为活动状态
			dprintf(INFO, "Active Slot: (%s)\n", SUFFIX_SLOT(boot_slot)); // 打印活动槽位信息
		}
	}

#if WITH_LK2ND
	// 如果启用了lk2nd（第二阶段引导加载程序），则进行初始化
	lk2nd_init();
#endif

	/* 如果启用则显示启动画面 */
	// 启动画面显示设备品牌logo或其他图形界面
#if DISPLAY_SPLASH_SCREEN
#if NO_ALARM_DISPLAY
	// 如果设置了不显示闹钟启动画面，则检查是否为闹钟启动
	if (!check_alarm_boot())
	{
#endif
		dprintf(SPEW, "Display Init: Start\n");
#if DISPLAY_HDMI_PRIMARY
		// 如果主显示设备是HDMI，则设置显示面板为HDMI
		if (!strlen(device.display_panel))
			strlcpy(device.display_panel, DISPLAY_PANEL_HDMI,
					sizeof(device.display_panel));
#endif
#if ENABLE_WBC
		/* 等待显示关闭完成 */
		// WBC (Wireless Battery Charging) 相关的显示电源管理
		while (pm_app_display_shutdown_in_prgs())
			;
		if (!pm_appsbl_display_init_done())
			target_display_init(device.display_panel); // 初始化目标平台显示
		else
			display_image_on_screen(); // 在屏幕上显示图像
#else
		target_display_init(device.display_panel); // 初始化显示面板
#endif
		dprintf(SPEW, "Display Init: Done\n");
#if NO_ALARM_DISPLAY
	}
#endif
#endif

	/* 获取设备序列号 */
	// 设备序列号用于唯一标识设备
	if (!IS_ENABLED(WITH_LK2ND) || !sn_buf[0])
		target_serialno((unsigned char *)sn_buf); // 获取硬件序列号
	dprintf(SPEW, "serial number: %s\n", sn_buf);

	// 清空显示面板缓冲区
	memset(display_panel_buf, '\0', MAX_PANEL_BUF_SIZE);

	/*
	 * 检查关机原因是否为用户强制重启，
	 * 如果是则进行正常启动。
	 */
	// 用户强制重启通常是通过长按电源键实现的
	if (is_user_force_reset())
		goto normal_boot; // 跳转到正常启动流程

	/* 检查是否需要执行除正常启动外的操作 */
	// 通过检测不同的按键组合来决定启动模式
	if (keys_get_state(KEY_VOLUMEUP) && keys_get_state(KEY_VOLUMEDOWN))
	{
		// 同时按下音量上键和下键进入紧急下载模式
		dprintf(ALWAYS, "dload mode key sequence detected\n");
		reboot_device(EMERGENCY_DLOAD); // 重启设备进入紧急下载模式
		dprintf(CRITICAL, "Failed to reboot into dload mode\n");

		boot_into_fastboot = true; // 如果失败则进入fastboot模式
	}
	if (!boot_into_fastboot)
	{
		// 检测单独的按键状态来决定启动模式
		if (keys_get_state(KEY_VOLUMEUP))
			boot_into_recovery = 1; // 音量上键进入恢复模式
		if (!boot_into_recovery &&
			(keys_get_state(KEY_HOME) || keys_get_state(KEY_BACK) || keys_get_state(KEY_VOLUMEDOWN)))
			boot_into_fastboot = true; // 其他按键组合进入fastboot模式
	}
#if NO_KEYPAD_DRIVER
	// 如果没有键盘驱动，则通过其他方式检测fastboot触发条件
	if (fastboot_trigger())
		boot_into_fastboot = true;
#endif

#if USE_PON_REBOOT_REG
	// 使用PON（Power On）寄存器检查硬重启模式
	reboot_mode = check_hard_reboot_mode();
#else
	// 检查普通的重启模式
	reboot_mode = check_reboot_mode();
#endif
	// 根据重启模式设置相应的启动标志
	if (reboot_mode == RECOVERY_MODE)
	{
		boot_into_recovery = 1; // 重启模式为恢复模式
	}
	else if (reboot_mode == FASTBOOT_MODE)
	{
		boot_into_fastboot = true; // 重启模式为fastboot模式
	}
	else if (reboot_mode == ALARM_BOOT)
	{
		boot_reason_alarm = true; // 重启原因为闹钟启动
	}
#if VERIFIED_BOOT || VERIFIED_BOOT_2
	// 如果启用了验证启动功能
	else if (VB_M <= target_get_vb_version())
	{
		if (reboot_mode == DM_VERITY_ENFORCING)
		{
			// 设置为强制验证模式
			device.verity_mode = 1;
			write_device_info(&device); // 将设置写入设备信息
		}
#if ENABLE_VB_ATTEST
		else if (reboot_mode == DM_VERITY_EIO)
#else
		else if (reboot_mode == DM_VERITY_LOGGING)
#endif
		{
			// 设置为日志记录模式
			device.verity_mode = 0;
			write_device_info(&device);
		}
		else if (reboot_mode == DM_VERITY_KEYSCLEAR)
		{
			// 清除验证密钥
			if (send_delete_keys_to_tz())
				ASSERT(0); // 如果清除失败则断言
		}
	}
#endif

#if LK2ND_FORCE_FASTBOOT
	// 如果强制启用lk2nd的fastboot模式
	boot_into_fastboot = true;
	dprintf(INFO, "Fastboot mode was forced with compile-time flag.\n");
#endif

normal_boot:
	// 正常启动流程
	if (!boot_into_fastboot)
	{
#if WITH_LK2ND_BOOT
		// 如果启用了lk2nd启动功能且不在恢复模式下
		if (!boot_into_recovery)
			lk2nd_boot(); // 执行lk2nd启动
#endif

		if (target_is_emmc_boot())
		{
			// 如果是从eMMC启动
			if (!IS_ENABLED(ABOOT_STANDALONE) && emmc_recovery_init())
				dprintf(ALWAYS, "error in emmc_recovery_init\n");
			if (target_use_signed_kernel())
			{
				// 如果使用签名内核
				if ((device.is_unlocked) || (device.is_tampered))
				{
					// 如果设备已解锁或被篡改
#ifdef TZ_TAMPER_FUSE
					set_tamper_fuse_cmd(HLOS_IMG_TAMPER_FUSE); // 设置篡改保险丝
#endif
#if USE_PCOM_SECBOOT
					set_tamper_flag(device.is_tampered); // 设置篡改标志
#endif
				}
			}

		retry_boot:
			/* 尝试从活动分区启动 */
			if (partition_multislot_is_supported())
			{
				// 在A/B分区系统中查找可启动的槽位
				boot_slot = partition_find_boot_slot();
				partition_mark_active_slot(boot_slot);
				if (boot_slot == INVALID)
					goto fastboot; // 如果没有可用槽位则进入fastboot
			}

			// 从MMC存储启动Linux系统
			boot_err_type = boot_linux_from_mmc();
			switch (boot_err_type)
			{
			case ERR_INVALID_PAGE_SIZE:
			case ERR_DT_PARSE:
			case ERR_ABOOT_ADDR_OVERLAP:
			case ERR_INVALID_BOOT_MAGIC:
				// 如果遇到这些错误且支持多槽位
				if (partition_multislot_is_supported())
				{
					/*
					 * 停用当前槽位（因为它无法启动）并尝试下一个槽位。
					 */
					partition_deactivate_slot(boot_slot);
					goto retry_boot; // 重新尝试启动
				}
				else
					break;
			default:
				break;
				/* 进入fastboot菜单 */
			}
		}
		else
		{
			// 如果不是从eMMC启动（如NAND启动）
			if (!IS_ENABLED(ABOOT_STANDALONE))
				recovery_init(); // 初始化恢复模式
#if USE_PCOM_SECBOOT
			if ((device.is_unlocked) || (device.is_tampered))
				set_tamper_flag(device.is_tampered); // 设置安全启动相关的篡改标志
#endif
			boot_linux_from_flash(); // 从闪存启动Linux
		}
		dprintf(CRITICAL, "ERROR: Could not do normal boot. Reverting "
						  "to fastboot mode.\n");
	}

fastboot:
	/* 到达此处表示常规启动未成功。启动fastboot模式。 */

	/* 注册aboot特定的fastboot命令 */
	fastboot_register_commands();        // 注册标准fastboot命令
	aboot_fastboot_register_commands();  // 注册aboot特有的fastboot命令

	/* 打印分区表用于调试 */
	if (target_is_emmc_boot())
		partition_dump(); // 打印eMMC分区信息

	/* 初始化并启动fastboot */
#if !VERIFIED_BOOT_2
	// 初始化fastboot，传入scratch地址和最大flash大小
	fastboot_init(target_get_scratch_address(), target_get_max_flash_size());
#else
	/* 在镜像地址开头添加salt缓冲区偏移量以复制VB salt */
	// 验证启动v2版本需要额外的salt缓冲区
	fastboot_init(ADD_SALT_BUFF_OFFSET(target_get_scratch_address()),
				  SUB_SALT_BUFF_OFFSET(target_get_max_flash_size()));
#endif
#if FBCON_DISPLAY_MSG || WITH_LK2ND_DEVICE_MENU
	display_fastboot_menu(); // 显示fastboot菜单界面
#endif
}