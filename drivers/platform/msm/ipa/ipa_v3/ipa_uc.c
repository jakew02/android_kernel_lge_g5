/* Copyright (c) 2012-2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include "ipa_i.h"
#include <linux/delay.h>

#define IPA_RAM_UC_SMEM_SIZE 128
#define IPA_HW_INTERFACE_VERSION     0x0111
#define IPA_PKT_FLUSH_TO_US 100
#define IPA_UC_POLL_SLEEP_USEC 100
#define IPA_UC_POLL_MAX_RETRY 10000

/**
 * Mailbox register to Interrupt HWP for CPU cmd
 * Usage of IPA_UC_MAILBOX_m_n doorbell instead of IPA_IRQ_EE_UC_0
 * due to HW limitation.
 *
 */
#define IPA_CPU_2_HW_CMD_MBOX_m          0
#define IPA_CPU_2_HW_CMD_MBOX_n         23

/**
 * enum ipa3_cpu_2_hw_commands - Values that represent the commands from the CPU
 * IPA_CPU_2_HW_CMD_NO_OP : No operation is required.
 * IPA_CPU_2_HW_CMD_UPDATE_FLAGS : Update SW flags which defines the behavior
 *                                 of HW.
 * IPA_CPU_2_HW_CMD_DEBUG_RUN_TEST : Launch predefined test over HW.
 * IPA_CPU_2_HW_CMD_DEBUG_GET_INFO : Read HW internal debug information.
 * IPA_CPU_2_HW_CMD_ERR_FATAL : CPU instructs HW to perform error fatal
 *                              handling.
 * IPA_CPU_2_HW_CMD_CLK_GATE : CPU instructs HW to goto Clock Gated state.
 * IPA_CPU_2_HW_CMD_CLK_UNGATE : CPU instructs HW to goto Clock Ungated state.
 * IPA_CPU_2_HW_CMD_MEMCPY : CPU instructs HW to do memcopy using QMB.
 * IPA_CPU_2_HW_CMD_RESET_PIPE : Command to reset a pipe - SW WA for a HW bug.
 */
enum ipa3_cpu_2_hw_commands {
	IPA_CPU_2_HW_CMD_NO_OP                     =
		FEATURE_ENUM_VAL(IPA_HW_FEATURE_COMMON, 0),
	IPA_CPU_2_HW_CMD_UPDATE_FLAGS              =
		FEATURE_ENUM_VAL(IPA_HW_FEATURE_COMMON, 1),
	IPA_CPU_2_HW_CMD_DEBUG_RUN_TEST            =
		FEATURE_ENUM_VAL(IPA_HW_FEATURE_COMMON, 2),
	IPA_CPU_2_HW_CMD_DEBUG_GET_INFO            =
		FEATURE_ENUM_VAL(IPA_HW_FEATURE_COMMON, 3),
	IPA_CPU_2_HW_CMD_ERR_FATAL                 =
		FEATURE_ENUM_VAL(IPA_HW_FEATURE_COMMON, 4),
	IPA_CPU_2_HW_CMD_CLK_GATE                  =
		FEATURE_ENUM_VAL(IPA_HW_FEATURE_COMMON, 5),
	IPA_CPU_2_HW_CMD_CLK_UNGATE                =
		FEATURE_ENUM_VAL(IPA_HW_FEATURE_COMMON, 6),
	IPA_CPU_2_HW_CMD_MEMCPY                    =
		FEATURE_ENUM_VAL(IPA_HW_FEATURE_COMMON, 7),
	IPA_CPU_2_HW_CMD_RESET_PIPE                =
		FEATURE_ENUM_VAL(IPA_HW_FEATURE_COMMON, 8),
	IPA_CPU_2_HW_CMD_REG_WRITE                 =
		FEATURE_ENUM_VAL(IPA_HW_FEATURE_COMMON, 9),
};

/**
 * enum ipa3_hw_2_cpu_responses -  Values that represent common HW responses
 * to CPU commands.
 * @IPA_HW_2_CPU_RESPONSE_INIT_COMPLETED : HW shall send this command once
 * boot sequence is completed and HW is ready to serve commands from CPU
 * @IPA_HW_2_CPU_RESPONSE_CMD_COMPLETED: Response to CPU commands
 */
enum ipa3_hw_2_cpu_responses {
	IPA_HW_2_CPU_RESPONSE_INIT_COMPLETED =
		FEATURE_ENUM_VAL(IPA_HW_FEATURE_COMMON, 1),
	IPA_HW_2_CPU_RESPONSE_CMD_COMPLETED  =
		FEATURE_ENUM_VAL(IPA_HW_FEATURE_COMMON, 2),
};

/**
 * enum ipa3_hw_2_cpu_events - Values that represent HW event to be sent to CPU.
 * @IPA_HW_2_CPU_EVENT_ERROR : Event specify a system error is detected by the
 * device
 * @IPA_HW_2_CPU_EVENT_LOG_INFO : Event providing logging specific information
 */
enum ipa3_hw_2_cpu_events {
	IPA_HW_2_CPU_EVENT_ERROR     =
		FEATURE_ENUM_VAL(IPA_HW_FEATURE_COMMON, 1),
	IPA_HW_2_CPU_EVENT_LOG_INFO  =
		FEATURE_ENUM_VAL(IPA_HW_FEATURE_COMMON, 2),
};

/**
 * enum ipa3_hw_errors - Common error types.
 * @IPA_HW_ERROR_NONE : No error persists
 * @IPA_HW_INVALID_DOORBELL_ERROR : Invalid data read from doorbell
 * @IPA_HW_DMA_ERROR : Unexpected DMA error
 * @IPA_HW_FATAL_SYSTEM_ERROR : HW has crashed and requires reset.
 * @IPA_HW_INVALID_OPCODE : Invalid opcode sent
 * @IPA_HW_ZIP_ENGINE_ERROR : ZIP engine error
 */
enum ipa3_hw_errors {
	IPA_HW_ERROR_NONE              =
		FEATURE_ENUM_VAL(IPA_HW_FEATURE_COMMON, 0),
	IPA_HW_INVALID_DOORBELL_ERROR  =
		FEATURE_ENUM_VAL(IPA_HW_FEATURE_COMMON, 1),
	IPA_HW_DMA_ERROR               =
		FEATURE_ENUM_VAL(IPA_HW_FEATURE_COMMON, 2),
	IPA_HW_FATAL_SYSTEM_ERROR      =
		FEATURE_ENUM_VAL(IPA_HW_FEATURE_COMMON, 3),
	IPA_HW_INVALID_OPCODE          =
		FEATURE_ENUM_VAL(IPA_HW_FEATURE_COMMON, 4),
	IPA_HW_ZIP_ENGINE_ERROR        =
		FEATURE_ENUM_VAL(IPA_HW_FEATURE_COMMON, 5)
};

/**
 * struct IpaHwResetPipeCmdData_t - Structure holding the parameters
 * for IPA_CPU_2_HW_CMD_MEMCPY command.
 *
 * The parameters are passed as immediate params in the shared memory
 */
struct IpaHwMemCopyData_t  {
	u32 destination_addr;
	u32 source_addr;
	u32 dest_buffer_size;
	u32 source_buffer_size;
};

/**
 * union IpaHwResetPipeCmdData_t - Structure holding the parameters
 * for IPA_CPU_2_HW_CMD_RESET_PIPE command.
 * @pipeNum : Pipe number to be reset
 * @direction : 1 - IPA Producer, 0 - IPA Consumer
 * @reserved_02_03 : Reserved
 *
 * The parameters are passed as immediate params in the shared memory
 */
union IpaHwResetPipeCmdData_t {
	struct IpaHwResetPipeCmdParams_t {
		u8     pipeNum;
		u8     direction;
		u32    reserved_02_03;
	} __packed params;
	u32 raw32b;
} __packed;

/**
 * struct IpaHwRegWriteCmdData_t - holds the parameters for
 * IPA_CPU_2_HW_CMD_REG_WRITE command. Parameters are
 * sent as pointer (not direct) thus should be reside
 * in memory address accessible to HW
 * @RegisterAddress: RG10 register address where the value needs to be written
 * @RegisterValue: 32-Bit value to be written into the register
 */
struct IpaHwRegWriteCmdData_t {
	u32 RegisterAddress;
	u32 RegisterValue;
};

/**
 * union IpaHwCpuCmdCompletedResponseData_t - Structure holding the parameters
 * for IPA_HW_2_CPU_RESPONSE_CMD_COMPLETED response.
 * @originalCmdOp : The original command opcode
 * @status : 0 for success indication, otherwise failure
 * @reserved : Reserved
 *
 * Parameters are sent as 32b immediate parameters.
 */
union IpaHwCpuCmdCompletedResponseData_t {
	struct IpaHwCpuCmdCompletedResponseParams_t {
		u32 originalCmdOp:8;
		u32 status:8;
		u32 reserved:16;
	} __packed params;
	u32 raw32b;
} __packed;

/**
 * union IpaHwErrorEventData_t - HW->CPU Common Events
 * @errorType : Entered when a system error is detected by the HW. Type of
 * error is specified by IPA_HW_ERRORS
 * @reserved : Reserved
 */
union IpaHwErrorEventData_t {
	struct IpaHwErrorEventParams_t {
		u32 errorType:8;
		u32 reserved:24;
	} __packed params;
	u32 raw32b;
} __packed;

/**
 * union IpaHwUpdateFlagsCmdData_t - Structure holding the parameters for
 * IPA_CPU_2_HW_CMD_UPDATE_FLAGS command
 * @newFlags: SW flags defined the behavior of HW.
 *	This field is expected to be used as bitmask for enum ipa3_hw_flags
 */
union IpaHwUpdateFlagsCmdData_t {
	struct IpaHwUpdateFlagsCmdParams_t {
		u32 newFlags;
	} params;
	u32 raw32b;
};

/**
 * When resource group 10 limitation mitigation is enabled, uC send
 * cmd should be able to run in interrupt context, so using spin lock
 * instead of mutex.
 */
#define IPA3_UC_LOCK(flags)						 \
do {									 \
	if (ipa3_ctx->apply_rg10_wa)					 \
		spin_lock_irqsave(&ipa3_ctx->uc_ctx.uc_spinlock, flags); \
	else								 \
		mutex_lock(&ipa3_ctx->uc_ctx.uc_lock);			 \
} while (0)

#define IPA3_UC_UNLOCK(flags)						      \
do {									      \
	if (ipa3_ctx->apply_rg10_wa)					      \
		spin_unlock_irqrestore(&ipa3_ctx->uc_ctx.uc_spinlock, flags); \
	else								      \
		mutex_unlock(&ipa3_ctx->uc_ctx.uc_lock);		      \
} while (0)

struct ipa3_uc_hdlrs ipa3_uc_hdlrs[IPA_HW_NUM_FEATURES] = { { 0 } };

static inline const char *ipa_hw_error_str(enum ipa3_hw_errors err_type)
{
	const char *str;

	switch (err_type) {
	case IPA_HW_ERROR_NONE:
		str = "IPA_HW_ERROR_NONE";
		break;
	case IPA_HW_INVALID_DOORBELL_ERROR:
		str = "IPA_HW_INVALID_DOORBELL_ERROR";
		break;
	case IPA_HW_FATAL_SYSTEM_ERROR:
		str = "IPA_HW_FATAL_SYSTEM_ERROR";
		break;
	case IPA_HW_INVALID_OPCODE:
		str = "IPA_HW_INVALID_OPCODE";
		break;
	case IPA_HW_ZIP_ENGINE_ERROR:
		str = "IPA_HW_ZIP_ENGINE_ERROR";
		break;
	default:
		str = "INVALID ipa_hw_errors type";
	}

	return str;
}

static void ipa3_log_evt_hdlr(void)
{
	int i;

	if (!ipa3_ctx->uc_ctx.uc_event_top_ofst) {
		ipa3_ctx->uc_ctx.uc_event_top_ofst =
			ipa3_ctx->uc_ctx.uc_sram_mmio->eventParams;
		if (ipa3_ctx->uc_ctx.uc_event_top_ofst +
			sizeof(struct IpaHwEventLogInfoData_t) >=
			ipa3_ctx->ctrl->ipa_reg_base_ofst +
			IPA_SRAM_DIRECT_ACCESS_N_OFST_v3_0(0) +
			ipa3_ctx->smem_sz) {
				IPAERR("uc_top 0x%x outside SRAM\n",
					ipa3_ctx->uc_ctx.uc_event_top_ofst);
				goto bad_uc_top_ofst;
		}

		ipa3_ctx->uc_ctx.uc_event_top_mmio = ioremap(
			ipa3_ctx->ipa_wrapper_base +
			ipa3_ctx->uc_ctx.uc_event_top_ofst,
			sizeof(struct IpaHwEventLogInfoData_t));
		if (!ipa3_ctx->uc_ctx.uc_event_top_mmio) {
			IPAERR("fail to ioremap uc top\n");
			goto bad_uc_top_ofst;
		}

		for (i = 0; i < IPA_HW_NUM_FEATURES; i++) {
			if (ipa3_uc_hdlrs[i].ipa_uc_event_log_info_hdlr)
				ipa3_uc_hdlrs[i].ipa_uc_event_log_info_hdlr
					(ipa3_ctx->uc_ctx.uc_event_top_mmio);
		}
	} else {

		if (ipa3_ctx->uc_ctx.uc_sram_mmio->eventParams !=
			ipa3_ctx->uc_ctx.uc_event_top_ofst) {
				IPAERR("uc top ofst changed new=%u cur=%u\n",
					ipa3_ctx->uc_ctx.uc_sram_mmio->
						eventParams,
					ipa3_ctx->uc_ctx.uc_event_top_ofst);
		}
	}

	return;

bad_uc_top_ofst:
	ipa3_ctx->uc_ctx.uc_event_top_ofst = 0;
}

/**
 * ipa3_uc_state_check() - Check the status of the uC interface
 *
 * Return value: 0 if the uC is loaded, interface is initialized
 *               and there was no recent failure in one of the commands.
 *               A negative value is returned otherwise.
 */
int ipa3_uc_state_check(void)
{
	if (!ipa3_ctx->uc_ctx.uc_inited) {
		IPAERR("uC interface not initialized\n");
		return -EFAULT;
	}

	if (!ipa3_ctx->uc_ctx.uc_loaded) {
		IPAERR("uC is not loaded\n");
		return -EFAULT;
	}

	if (ipa3_ctx->uc_ctx.uc_failed) {
		IPAERR("uC has failed its last command\n");
		return -EFAULT;
	}

	return 0;
}

/**
 * ipa3_uc_loaded_check() - Check the uC has been loaded
 *
 * Return value: 1 if the uC is loaded, 0 otherwise
 */
int ipa3_uc_loaded_check(void)
{
	return ipa3_ctx->uc_ctx.uc_loaded;
}
EXPORT_SYMBOL(ipa3_uc_loaded_check);

static void ipa3_uc_event_handler(enum ipa_irq_type interrupt,
				 void *private_data,
				 void *interrupt_data)
{
	union IpaHwErrorEventData_t evt;
	u8 feature;

	WARN_ON(private_data != ipa3_ctx);

	ipa3_inc_client_enable_clks();

	IPADBG("uC evt opcode=%u\n",
		ipa3_ctx->uc_ctx.uc_sram_mmio->eventOp);


	feature = EXTRACT_UC_FEATURE(ipa3_ctx->uc_ctx.uc_sram_mmio->eventOp);

	if (0 > feature || IPA_HW_FEATURE_MAX <= feature) {
		IPAERR("Invalid feature %u for event %u\n",
			feature, ipa3_ctx->uc_ctx.uc_sram_mmio->eventOp);
		ipa3_dec_client_disable_clks();
		return;
	}
	/* Feature specific handling */
	if (ipa3_uc_hdlrs[feature].ipa_uc_event_hdlr)
		ipa3_uc_hdlrs[feature].ipa_uc_event_hdlr
			(ipa3_ctx->uc_ctx.uc_sram_mmio);

	/* General handling */
	if (ipa3_ctx->uc_ctx.uc_sram_mmio->eventOp ==
	    IPA_HW_2_CPU_EVENT_ERROR) {
		evt.raw32b = ipa3_ctx->uc_ctx.uc_sram_mmio->eventParams;
		IPAERR("uC Error, evt errorType = %s\n",
			ipa_hw_error_str(evt.params.errorType));
		ipa3_ctx->uc_ctx.uc_failed = true;
		ipa3_ctx->uc_ctx.uc_error_type = evt.params.errorType;
		if (evt.params.errorType == IPA_HW_ZIP_ENGINE_ERROR) {
			IPAERR("IPA has encountered a ZIP engine error\n");
			ipa3_ctx->uc_ctx.uc_zip_error = true;
		}
		BUG();
	} else if (ipa3_ctx->uc_ctx.uc_sram_mmio->eventOp ==
		IPA_HW_2_CPU_EVENT_LOG_INFO) {
			IPADBG("uC evt log info ofst=0x%x\n",
				ipa3_ctx->uc_ctx.uc_sram_mmio->eventParams);
		ipa3_log_evt_hdlr();
	} else {
		IPADBG("unsupported uC evt opcode=%u\n",
				ipa3_ctx->uc_ctx.uc_sram_mmio->eventOp);
	}
	ipa3_dec_client_disable_clks();

}

static int ipa3_uc_panic_notifier(struct notifier_block *this,
		unsigned long event, void *ptr)
{
	int result = 0;

	IPADBG("this=%p evt=%lu ptr=%p\n", this, event, ptr);

	result = ipa3_uc_state_check();
	if (result)
		goto fail;

	if (ipa3_inc_client_enable_clks_no_block())
		goto fail;

	ipa3_ctx->uc_ctx.uc_sram_mmio->cmdOp =
		IPA_CPU_2_HW_CMD_ERR_FATAL;
	ipa3_ctx->uc_ctx.pending_cmd = ipa3_ctx->uc_ctx.uc_sram_mmio->cmdOp;
	/* ensure write to shared memory is done before triggering uc */
	wmb();

	if (ipa3_ctx->apply_rg10_wa)
		ipa_write_reg(ipa3_ctx->mmio,
			IPA_UC_MAILBOX_m_n_OFFS_v3_0(IPA_CPU_2_HW_CMD_MBOX_m,
			IPA_CPU_2_HW_CMD_MBOX_n), 0x1);
	else
		ipa_write_reg(ipa3_ctx->mmio, IPA_IRQ_EE_UC_n_OFFS(0), 0x1);

	/* give uc enough time to save state */
	udelay(IPA_PKT_FLUSH_TO_US);

	ipa3_dec_client_disable_clks();
	IPADBG("err_fatal issued\n");

fail:
	return NOTIFY_DONE;
}

static struct notifier_block ipa3_uc_panic_blk = {
	.notifier_call  = ipa3_uc_panic_notifier,
};

void ipa3_register_panic_hdlr(void)
{
	atomic_notifier_chain_register(&panic_notifier_list,
			&ipa3_uc_panic_blk);
}

static void ipa3_uc_response_hdlr(enum ipa_irq_type interrupt,
				void *private_data,
				void *interrupt_data)
{
	union IpaHwCpuCmdCompletedResponseData_t uc_rsp;
	u8 feature;
	int res;
	int i;

	WARN_ON(private_data != ipa3_ctx);

	ipa3_inc_client_enable_clks();
	IPADBG("uC rsp opcode=%u\n",
			ipa3_ctx->uc_ctx.uc_sram_mmio->responseOp);

	feature = EXTRACT_UC_FEATURE(ipa3_ctx->uc_ctx.uc_sram_mmio->responseOp);

	if (0 > feature || IPA_HW_FEATURE_MAX <= feature) {
		IPAERR("Invalid feature %u for event %u\n",
			feature, ipa3_ctx->uc_ctx.uc_sram_mmio->eventOp);
		ipa3_dec_client_disable_clks();
		return;
	}

	/* Feature specific handling */
	if (ipa3_uc_hdlrs[feature].ipa3_uc_response_hdlr) {
		res = ipa3_uc_hdlrs[feature].ipa3_uc_response_hdlr(
			ipa3_ctx->uc_ctx.uc_sram_mmio,
			&ipa3_ctx->uc_ctx.uc_status);
		if (res == 0) {
			IPADBG("feature %d specific response handler\n",
				feature);
			complete_all(&ipa3_ctx->uc_ctx.uc_completion);
			ipa3_dec_client_disable_clks();
			return;
		}
	}

	/* General handling */
	if (ipa3_ctx->uc_ctx.uc_sram_mmio->responseOp ==
			IPA_HW_2_CPU_RESPONSE_INIT_COMPLETED) {
		ipa3_ctx->uc_ctx.uc_loaded = true;

		IPADBG("IPA uC loaded\n");
		/*
		 * The proxy vote is held until uC is loaded to ensure that
		 * IPA_HW_2_CPU_RESPONSE_INIT_COMPLETED is received.
		 */
		ipa3_proxy_clk_unvote();

		for (i = 0; i < IPA_HW_NUM_FEATURES; i++) {
			if (ipa3_uc_hdlrs[i].ipa_uc_loaded_hdlr)
				ipa3_uc_hdlrs[i].ipa_uc_loaded_hdlr();
		}
	} else if (ipa3_ctx->uc_ctx.uc_sram_mmio->responseOp ==
		   IPA_HW_2_CPU_RESPONSE_CMD_COMPLETED) {
		uc_rsp.raw32b = ipa3_ctx->uc_ctx.uc_sram_mmio->responseParams;
		IPADBG("uC cmd response opcode=%u status=%u\n",
		       uc_rsp.params.originalCmdOp,
		       uc_rsp.params.status);
		if (uc_rsp.params.originalCmdOp ==
		    ipa3_ctx->uc_ctx.pending_cmd) {
			ipa3_ctx->uc_ctx.uc_status = uc_rsp.params.status;
			complete_all(&ipa3_ctx->uc_ctx.uc_completion);
		} else {
			IPAERR("Expected cmd=%u rcvd cmd=%u\n",
			       ipa3_ctx->uc_ctx.pending_cmd,
			       uc_rsp.params.originalCmdOp);
		}
	} else {
		IPAERR("Unsupported uC rsp opcode = %u\n",
		       ipa3_ctx->uc_ctx.uc_sram_mmio->responseOp);
	}
	ipa3_dec_client_disable_clks();
}

/**
 * ipa3_uc_interface_init() - Initialize the interface with the uC
 *
 * Return value: 0 on success, negative value otherwise
 */
int ipa3_uc_interface_init(void)
{
	int result;
	unsigned long phys_addr;

	if (ipa3_ctx->uc_ctx.uc_inited) {
		IPADBG("uC interface already initialized\n");
		return 0;
	}

	mutex_init(&ipa3_ctx->uc_ctx.uc_lock);
	spin_lock_init(&ipa3_ctx->uc_ctx.uc_spinlock);

	phys_addr = ipa3_ctx->ipa_wrapper_base +
		ipa3_ctx->ctrl->ipa_reg_base_ofst +
		IPA_SRAM_DIRECT_ACCESS_N_OFST_v3_0(0);
	ipa3_ctx->uc_ctx.uc_sram_mmio = ioremap(phys_addr,
					       IPA_RAM_UC_SMEM_SIZE);
	if (!ipa3_ctx->uc_ctx.uc_sram_mmio) {
		IPAERR("Fail to ioremap IPA uC SRAM\n");
		result = -ENOMEM;
		goto remap_fail;
	}

	if (!ipa3_ctx->apply_rg10_wa) {
		result = ipa3_add_interrupt_handler(IPA_UC_IRQ_0,
			ipa3_uc_event_handler, true,
			ipa3_ctx);
		if (result) {
			IPAERR("Fail to register for UC_IRQ0 rsp interrupt\n");
			result = -EFAULT;
			goto irq_fail0;
		}

		result = ipa3_add_interrupt_handler(IPA_UC_IRQ_1,
			ipa3_uc_response_hdlr, true,
			ipa3_ctx);
		if (result) {
			IPAERR("fail to register for UC_IRQ1 rsp interrupt\n");
			result = -EFAULT;
			goto irq_fail1;
		}
	}

	ipa3_ctx->uc_ctx.uc_inited = true;

	IPADBG("IPA uC interface is initialized\n");
	return 0;

irq_fail1:
	ipa3_remove_interrupt_handler(IPA_UC_IRQ_0);
irq_fail0:
	iounmap(ipa3_ctx->uc_ctx.uc_sram_mmio);
remap_fail:
	return result;
}

/**
 * ipa3_uc_load_notify() - Notification about uC loading
 *
 * This function should be called when IPA uC interface layer cannot
 * determine by itself about uC loading by waits for external notification.
 * Example is resource group 10 limitation were ipa driver does not get uC
 * interrupts.
 * The function should perform actions that were not done at init due to uC
 * not being loaded then.
 *
 */
void ipa3_uc_load_notify(void)
{
	int i;
	int result;

	if (!ipa3_ctx->apply_rg10_wa)
		return;

	ipa3_ctx->uc_ctx.uc_loaded = true;
	IPADBG("IPA uC loaded\n");

	ipa3_proxy_clk_unvote();

	ipa3_init_interrupts();

	result = ipa3_add_interrupt_handler(IPA_UC_IRQ_0,
		ipa3_uc_event_handler, true,
		ipa3_ctx);
	if (result)
		IPAERR("Fail to register for UC_IRQ0 rsp interrupt.\n");

	for (i = 0; i < IPA_HW_NUM_FEATURES; i++) {
		if (ipa3_uc_hdlrs[i].ipa_uc_loaded_hdlr)
			ipa3_uc_hdlrs[i].ipa_uc_loaded_hdlr();
	}
}
EXPORT_SYMBOL(ipa3_uc_load_notify);

/**
 * ipa3_uc_send_cmd() - Send a command to the uC
 *
 * Note: In case the operation times out (No response from the uC) or
 *       polling maximal amount of retries has reached, the logic
 *       considers it as an invalid state of the uC/IPA, and
 *       issues a kernel panic.
 *
 * Returns: 0 on success.
 *          -EINVAL in case of invalid input.
 *          -EBADF in case uC interface is not initialized /
 *                 or the uC has failed previously.
 *          -EFAULT in case the received status doesn't match
 *                  the expected.
 */
int ipa3_uc_send_cmd(u32 cmd, u32 opcode, u32 expected_status,
		    bool polling_mode, unsigned long timeout_jiffies)
{
	int index;
	union IpaHwCpuCmdCompletedResponseData_t uc_rsp;
	unsigned long flags;

	IPA3_UC_LOCK(flags);

	if (ipa3_uc_state_check()) {
		IPADBG("uC send command aborted\n");
		IPA3_UC_UNLOCK(flags);
		return -EBADF;
	}

	if (ipa3_ctx->apply_rg10_wa) {
		if (!polling_mode)
			IPADBG("Overriding mode to polling mode\n");
		polling_mode = true;
	} else {
		init_completion(&ipa3_ctx->uc_ctx.uc_completion);
	}

	ipa3_ctx->uc_ctx.uc_sram_mmio->cmdParams = cmd;
	ipa3_ctx->uc_ctx.uc_sram_mmio->cmdOp = opcode;
	ipa3_ctx->uc_ctx.pending_cmd = opcode;

	ipa3_ctx->uc_ctx.uc_sram_mmio->responseOp = 0;
	ipa3_ctx->uc_ctx.uc_sram_mmio->responseParams = 0;

	ipa3_ctx->uc_ctx.uc_status = 0;

	/* ensure write to shared memory is done before triggering uc */
	wmb();

	if (ipa3_ctx->apply_rg10_wa)
		ipa_write_reg(ipa3_ctx->mmio,
			IPA_UC_MAILBOX_m_n_OFFS_v3_0(IPA_CPU_2_HW_CMD_MBOX_m,
			IPA_CPU_2_HW_CMD_MBOX_n), 0x1);
	else
		ipa_write_reg(ipa3_ctx->mmio, IPA_IRQ_EE_UC_n_OFFS(0), 0x1);

	if (polling_mode) {
		for (index = 0; index < IPA_UC_POLL_MAX_RETRY; index++) {
			if (ipa3_ctx->uc_ctx.uc_sram_mmio->responseOp ==
			    IPA_HW_2_CPU_RESPONSE_CMD_COMPLETED) {
				uc_rsp.raw32b = ipa3_ctx->uc_ctx.uc_sram_mmio->
						responseParams;
				if (uc_rsp.params.originalCmdOp ==
					ipa3_ctx->uc_ctx.pending_cmd) {
					ipa3_ctx->uc_ctx.uc_status =
						uc_rsp.params.status;
					break;
				}
			}
			if (ipa3_ctx->apply_rg10_wa)
				udelay(IPA_UC_POLL_SLEEP_USEC);
			else
				usleep_range(IPA_UC_POLL_SLEEP_USEC,
					IPA_UC_POLL_SLEEP_USEC);
		}

		if (index == IPA_UC_POLL_MAX_RETRY) {
			IPAERR("uC max polling retries reached\n");
			if (ipa3_ctx->uc_ctx.uc_failed) {
				IPAERR("uC reported on Error, errorType = %s\n",
					ipa_hw_error_str(ipa3_ctx->
					uc_ctx.uc_error_type));
			}
			IPA3_UC_UNLOCK(flags);
			BUG();
			return -EFAULT;
		}
	} else {
		if (wait_for_completion_timeout(&ipa3_ctx->uc_ctx.uc_completion,
			timeout_jiffies) == 0) {
			IPAERR("uC timed out\n");
			if (ipa3_ctx->uc_ctx.uc_failed) {
				IPAERR("uC reported on Error, errorType = %s\n",
					ipa_hw_error_str(ipa3_ctx->
					uc_ctx.uc_error_type));
			}
			IPA3_UC_UNLOCK(flags);
			BUG();
			return -EFAULT;
		}
	}

	if (ipa3_ctx->uc_ctx.uc_status != expected_status) {
		IPAERR("Recevied status %u, Expected status %u\n",
			ipa3_ctx->uc_ctx.uc_status, expected_status);
		IPA3_UC_UNLOCK(flags);
		return -EFAULT;
	}

	IPA3_UC_UNLOCK(flags);

	IPADBG("uC cmd %u send succeeded\n", opcode);

	return 0;
}

/**
 * ipa3_uc_register_handlers() - Registers event, response and log event
 *                              handlers for a specific feature.Please note
 *                              that currently only one handler can be
 *                              registered per feature.
 *
 * Return value: None
 */
void ipa3_uc_register_handlers(enum ipa3_hw_features feature,
			      struct ipa3_uc_hdlrs *hdlrs)
{
	unsigned long flags;

	if (0 > feature || IPA_HW_FEATURE_MAX <= feature) {
		IPAERR("Feature %u is invalid, not registering hdlrs\n",
		       feature);
		return;
	}

	IPA3_UC_LOCK(flags);
	ipa3_uc_hdlrs[feature] = *hdlrs;
	IPA3_UC_UNLOCK(flags);

	IPADBG("uC handlers registered for feature %u\n", feature);
}

/**
 * ipa3_uc_reset_pipe() - reset a BAM pipe using the uC interface
 * @ipa_client: [in] ipa client handle representing the pipe
 *
 * The function uses the uC interface in order to issue a BAM
 * PIPE reset request. The uC makes sure there's no traffic in
 * the TX command queue before issuing the reset.
 *
 * Returns:	0 on success, negative on failure
 */
int ipa3_uc_reset_pipe(enum ipa_client_type ipa_client)
{
	union IpaHwResetPipeCmdData_t cmd;
	int ep_idx;
	int ret;

	ep_idx = ipa3_get_ep_mapping(ipa_client);
	if (ep_idx == -1) {
		IPAERR("Invalid IPA client\n");
		return 0;
	}

	/*
	 * If the uC interface has not been initialized yet,
	 * continue with the sequence without resetting the
	 * pipe.
	 */
	if (ipa3_uc_state_check()) {
		IPADBG("uC interface will not be used to reset %s pipe %d\n",
		       IPA_CLIENT_IS_PROD(ipa_client) ? "CONS" : "PROD",
		       ep_idx);
		return 0;
	}

	/*
	 * IPA consumer = 0, IPA producer = 1.
	 * IPA driver concept of PROD/CONS is the opposite of the
	 * IPA HW concept. Therefore, IPA AP CLIENT PRODUCER = IPA CONSUMER,
	 * and vice-versa.
	 */
	cmd.params.direction = (u8)(IPA_CLIENT_IS_PROD(ipa_client) ? 0 : 1);
	cmd.params.pipeNum = (u8)ep_idx;

	IPADBG("uC pipe reset on IPA %s pipe %d\n",
	       IPA_CLIENT_IS_PROD(ipa_client) ? "CONS" : "PROD", ep_idx);

	ret = ipa3_uc_send_cmd(cmd.raw32b, IPA_CPU_2_HW_CMD_RESET_PIPE, 0,
			      false, 10*HZ);

	return ret;
}

/**
 * ipa3_uc_notify_clk_state() - notify to uC of clock enable / disable
 * @enabled: true if clock are enabled
 *
 * The function uses the uC interface in order to notify uC before IPA clocks
 * are disabled to make sure uC is not in the middle of operation.
 * Also after clocks are enabled ned to notify uC to start processing.
 *
 * Returns: 0 on success, negative on failure
 */
int ipa3_uc_notify_clk_state(bool enabled)
{
	u32 opcode;

	/*
	 * If the uC interface has not been initialized yet,
	 * don't notify the uC on the enable/disable
	 */
	if (ipa3_uc_state_check()) {
		IPADBG("uC interface will not notify the UC on clock state\n");
		return 0;
	}

	IPADBG("uC clock %s notification\n", (enabled) ? "UNGATE" : "GATE");

	opcode = (enabled) ? IPA_CPU_2_HW_CMD_CLK_UNGATE :
			     IPA_CPU_2_HW_CMD_CLK_GATE;

	return ipa3_uc_send_cmd(0, opcode, 0, true, 0);
}

/**
 * ipa3_uc_update_hw_flags() - send uC the HW flags to be used
 * @flags: This field is expected to be used as bitmask for enum ipa3_hw_flags
 *
 * Returns: 0 on success, negative on failure
 */
int ipa3_uc_update_hw_flags(u32 flags)
{
	union IpaHwUpdateFlagsCmdData_t cmd;

	memset(&cmd, 0, sizeof(cmd));
	cmd.params.newFlags = flags;
	return ipa3_uc_send_cmd(cmd.raw32b, IPA_CPU_2_HW_CMD_UPDATE_FLAGS, 0,
		false, HZ);
}

/**
 * ipa3_uc_rg10_write_reg() - write to register possibly via uC
 *
 * if the RG10 limitation workaround is enabled, then writing
 * to a register will be proxied by the uC due to H/W limitation.
 * This func should be called for RG10 registers only
 *
 * @Parameters: Like ipa_write_reg() parameters
 *
 */
void ipa3_uc_rg10_write_reg(void *base, u32 offset, u32 val)
{
	struct ipa3_mem_buffer reg_data_mem = {0};
	struct IpaHwRegWriteCmdData_t *reg_data;
	int ret = 0;
	u32 pbase;

	if (!ipa3_ctx->apply_rg10_wa)
		return ipa_write_reg(base, offset, val);

	reg_data_mem.size = sizeof(struct IpaHwRegWriteCmdData_t);
	reg_data_mem.base = dma_alloc_coherent(ipa3_ctx->uc_pdev,
		reg_data_mem.size, &reg_data_mem.phys_base,
		GFP_ATOMIC | __GFP_REPEAT);
	if (!reg_data_mem.base) {
		IPAERR("fail to alloc DMA buff of size %d\n",
			reg_data_mem.size);
		BUG();
	}

	/* calculate physical base address */
	pbase = ipa3_ctx->ipa_wrapper_base + ipa3_ctx->ctrl->ipa_reg_base_ofst;

	reg_data = reg_data_mem.base;
	reg_data->RegisterAddress = pbase + offset;
	reg_data->RegisterValue = val;

	IPADBG("Sending uC cmd to reg write: addr=0x%x val=0x%x\n",
		reg_data->RegisterAddress, val);
	ret = ipa3_uc_send_cmd((u32)reg_data_mem.phys_base,
		IPA_CPU_2_HW_CMD_REG_WRITE, 0, true, 0);
	if (ret) {
		IPAERR("failed to send cmd to uC for reg write\n");
		BUG();
	}

	dma_free_coherent(ipa3_ctx->uc_pdev, reg_data_mem.size,
		reg_data_mem.base, reg_data_mem.phys_base);
}

/**
 * ipa3_uc_memcpy() - Perform a memcpy action using IPA uC
 * @dest: physical address to store the copied data.
 * @src: physical address of the source data to copy.
 * @len: number of bytes to copy.
 *
 * Returns: 0 on success, negative on failure
 */
int ipa3_uc_memcpy(phys_addr_t dest, phys_addr_t src, int len)
{
	int res;
	struct ipa3_mem_buffer mem;
	struct IpaHwMemCopyData_t *cmd;

	IPADBG("dest 0x%pa src 0x%pa len %d\n", &dest, &src, len);
	mem.size = sizeof(cmd);
	mem.base = dma_alloc_coherent(ipa3_ctx->pdev, mem.size, &mem.phys_base,
		GFP_KERNEL);
	if (!mem.base) {
		IPAERR("fail to alloc DMA buff of size %d\n", mem.size);
		return -ENOMEM;
	}
	cmd = (struct IpaHwMemCopyData_t *)mem.base;
	memset(cmd, 0, sizeof(*cmd));
	cmd->destination_addr = dest;
	cmd->dest_buffer_size = len;
	cmd->source_addr = src;
	cmd->source_buffer_size = len;
	res = ipa3_uc_send_cmd((u32)mem.phys_base, IPA_CPU_2_HW_CMD_MEMCPY, 0,
		true, 10 * HZ);
	if (res) {
		IPAERR("ipa3_uc_send_cmd failed %d\n", res);
		goto free_coherent;
	}

	res = 0;
free_coherent:
	dma_free_coherent(ipa3_ctx->pdev, mem.size, mem.base, mem.phys_base);
	return res;
}
