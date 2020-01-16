/*
* ***************************************************************************
* Copyright (C) 2017 Marvell International Ltd.
* ***************************************************************************
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* Redistributions of source code must retain the above copyright notice, this
* list of conditions and the following disclaimer.
*
* Redistributions in binary form must reproduce the above copyright notice,
* this list of conditions and the following disclaimer in the documentation
* and/or other materials provided with the distribution.
*
* Neither the name of Marvell nor the names of its contributors may be used
* to endorse or promote products derived from this software without specific
* prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
* ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
* LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
* OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
* INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
* CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
* ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
* POSSIBILITY OF SUCH DAMAGE.
*
***************************************************************************
*/

#include "../sys.h"
#include "ddr.h"
#include "ddr_support.h"
#include <stdbool.h>

#undef VALIDATION_EYE

#define DDR3_QSGATING
#define DDR4_VREF_TRAINING

#define BYTE_ERROR1(a, b, mask)	(((a) & (mask)) != ((b) & (mask)))

#ifdef VALIDATION_EYE
void rx_sweep_test(void);
static int cpu_test(int byte_mask);

static const unsigned int pattern[] = {
	0x0101fefe,
	0xfefe0101,
	0x0202fdfd,
	0xfdfd0202,
	0x0404fbfb,
	0xfbfb0404,
	0x0808f7f7,
	0xf7f70808,
	0x1010efef,
	0xefef1010,
	0x2020dfdf,
	0xdfdf2020,
	0x4040bfbf,
	0xbfbf4040,
	0x80807f7f,
	0x7f7f8080,
	0xf7f78080,
	0x8080f7f7,
	0xf7f7fbfb,
	0xfbfbf7f7,
	0x40407f7f,
	0x7f7f4040,
	0x80804040,
	0x80804040,
	0xbfbf4040,
	0xbfbf4040,
	0xdfdf2020,
	0x2020dfdf,
	0xdfdf2020,
	0x1010efef,
	0x1010efef,
	0x10108080,
	0x80801010,
	0xf7f70808,
	0x0808f7f7,
	0x0404fbfb,
	0xfbfb4040,
	0x4040fbfb,
	0x2020fdfd,
	0xfdfd2020,
	0x2020fdfd,
	0x1010efef,
	0xfefe0101,
	0x10108080,
	0x80800101,
	0x7f7f8080,
	0x80807f7f,
	0x4040bfbf,
	0xbfbf4040,
	0x40402020,
	0x20204040,
	0xdfdf2020,
	0x2020dfdf,
	0x1010efef,
	0xefef1010,
	0xffff0000,
	0x0000ffff,
	0xffff0000,
	0x0000ffff,
	0xffff0000,
	0x0000ffff,
	0xffff0000,
	0x00000000,
	0x00000000,
	0xffff0000,
	0x0000ffff,
	0xffff0000,
	0x0000ffff,
	0xffff0000,
	0x0000ffff,
	0xffff0000
};
#endif

enum ddr_type tc_ddr_type;
unsigned int tc_cs_num;
int debug_level = 0;
int debug_module = 0;

int set_ddr_type(enum ddr_type type){
	if(type >= DDR_TYPE_MAX)
		return -1;
	else
	{
		tc_ddr_type = type;
		return 0;
	}
}

int set_ddr_topology_parameters(struct ddr_topology top_map){
	tc_cs_num = top_map.cs_num;
	return 0;
}

int init_ddr(struct ddr_init_para init_para,
	struct ddr_init_result *result){

	unsigned int cs;
	unsigned int zp_zn_trm = 0x0;
	unsigned int dll_res;
	int ret_val=0;
#ifdef DDR3_QSGATING
	unsigned int qs_res[MAX_CS_NUM];
	unsigned int disable_rl;
#endif
#ifdef DDR4_VREF_TRAINING
	int vdac_value = 0;
	unsigned int vref_value = 0;
#endif
	debug_level = init_para.log_level;
	debug_module = init_para.flags;

	printf("");

	/* Write patterns at slow speed before going into Self Refresh */
	//TODO: ddr_test_dma(0x1000) - size, base addr - Is it needed if I do self_refresh_test(0)

	/*
	 * The self-refresh test should applied to the code boot only.
	 * In a warm boot, DRAM holds the user data and Linux content
	 * of the previous boot. The test may corrupt the existing
	 * data unexpectly.
	 */
	if (!init_para.warm_boot)
		/*
		 * The CM3's address map to DRAM is supplied, which is
		 * available for memory test. But the boot image is pre-
		 * loaded and located at 0x0041.0000. Avoiding wripping
		 * out the image data, only fill the test pattern to the
		 * first 1KB memory per each chip select and validate by read
		 * that the pattern is correct.
		 */
		for(cs=0; cs<tc_cs_num; cs++)
			self_refresh_test(1, init_para.cs_wins[cs].base, 1024);

	/* 0. Drain WCB and check for various FIFOs to make sure they are all empty */
	wait_for_mc_idle();

	/* 1. enter self refresh */
	for (cs = 0; cs < tc_cs_num; cs++)
		self_refresh_entry(cs, tc_ddr_type);

	/* 2. setup clock */
	init_para.clock_init();

	/* 3. DDRPHY sync2 and DLL reset */
	LogMsg(LOG_LEVEL_INFO, FLAG_REGS_PHYINIT_SYNC2, "\nBefore phyinit_sequence_sync2:");
	logs_training_regs(PHYINIT_SYNC2);
	phyinit_sequence_sync2(1, 3, 2, 0);
	LogMsg(LOG_LEVEL_INFO, FLAG_REGS_PHYINIT_SYNC2, "\nAfter phyinit_sequence_sync2:");
	logs_training_regs(PHYINIT_SYNC2);

	/* 4. update CL/CWL *nd other timing parameters as per lookup table */
	/* Skip it, use the current settings in register */
	LogMsg(LOG_LEVEL_INFO, FLAG_REGS_INIT_TIMING, "\nLatency:");
	logs_training_regs(INIT_TIMING);

	/* 5. enable DDRPHY termination */
	if(tc_ddr_type == DDR3)
	{
		zp_zn_trm = 0xC4477889 & 0x0FF00000;	//PHY_Control_2[0xC0001004]: copied from TIM
		LogMsg(LOG_LEVEL_INFO, FLAG_REGS_TERM, "\nBefore restoring termination:");
		logs_training_regs(TERM);
		set_clear_trm(1, zp_zn_trm);
		LogMsg(LOG_LEVEL_INFO, FLAG_REGS_TERM, "\nAfter restoring termination:");
		logs_training_regs(TERM);
	}

	/* Step 6 and 7 - Not needed as per Suresh */
	/* 6. enable DRAM termination , set DRAM DLL */
	//dll_on_dram();
	/* 7. enable DLL on for ddrphy and DLL reset */
	//dll_on_ddrphy();

	/* 8. exit self refresh */
	for (cs = 0; cs < tc_cs_num; cs++)
		self_refresh_exit(cs);

	/* 9. do MR command */
	send_mr_commands(tc_ddr_type);


	if(init_para.warm_boot)
        {
                LogMsg(LOG_LEVEL_ERROR, FLAG_WARM_BOOT, "\nWARM BOOT SET");
                if(tc_ddr_type == DDR3)
                {
#ifdef DDR3_QSGATING
                        //qs gate
                        ll_write32(CH0_PHY_WL_RL_Control, result->ddr3.wl_rl_ctl);
                        ll_write32(CH0_PHY_RL_Control_CS0_B0, result->ddr3.cs0_b0);
                        ll_write32(CH0_PHY_RL_Control_CS0_B1, result->ddr3.cs0_b1);
                        ll_write32(CH0_PHY_RL_Control_CS1_B0, result->ddr3.cs1_b0);
                        ll_write32(CH0_PHY_RL_Control_CS1_B1, result->ddr3.cs1_b1);
			disable_rl = result->ddr3.wl_rl_ctl & (~0x3);
			ll_write32(CH0_PHY_WL_RL_Control, disable_rl);
#endif
                }
                else if(tc_ddr_type == DDR4)
                {
#ifdef DDR4_VREF_TRAINING
                        //vref read
                        ll_write32(PHY_Control_15, result->ddr4.vref_read);
			wait_ns(1000);

                        //vref write
                        en_dis_write_vref(1);
			ll_write32(CH0_DRAM_Config_4, result->ddr4.vref_write);
                        ll_write32(USER_COMMAND_2, 0x13004000);
                        wait_ns(1000);
                        en_dis_write_vref(0);
#endif
                }
                //dll tuning
                ll_write32(CH0_PHY_DLL_control_B0, result->dll_tune.dll_ctrl_b0);
                ll_write32(CH0_PHY_DLL_control_B1, result->dll_tune.dll_ctrl_b1);
                ll_write32(CH0_PHY_DLL_control_ADCM, result->dll_tune.dll_ctrl_adcm);

                LogMsg(LOG_LEVEL_ERROR, FLAG_WARM_BOOT, "\nWARM BOOT COMPLETED");
                return 0;
        }

	/* QS Gate Training/ Read Leveling - for DDR3 only*/
	//Capture settings from TIM, update only if training passes.
	result->ddr3.wl_rl_ctl = ll_read32(CH0_PHY_WL_RL_Control);
        result->ddr3.cs0_b0 = ll_read32(CH0_PHY_RL_Control_CS0_B0);
        result->ddr3.cs0_b1 = ll_read32(CH0_PHY_RL_Control_CS0_B1);
        result->ddr3.cs1_b0 = ll_read32(CH0_PHY_RL_Control_CS1_B0);
        result->ddr3.cs1_b1 = ll_read32(CH0_PHY_RL_Control_CS1_B1);
#ifdef DDR3_QSGATING
	if(tc_ddr_type == DDR3)
        {
		LogMsg(LOG_LEVEL_INFO, FLAG_REGS_QS_GATE, "\n\nQS GATING\n=============");
		LogMsg(LOG_LEVEL_INFO, FLAG_REGS_QS_GATE, "\nBefore QS gating:");
		logs_training_regs(QS_GATE);
		for(cs=0; cs<tc_cs_num; cs++)
		{
			qs_res[cs] = qs_gating(init_para.cs_wins[cs].base, cs, result);
			if(qs_res[cs])
				LogMsg(LOG_LEVEL_ERROR, FLAG_REGS_QS_GATE, "\nCS%d: QS GATE TRAINING PASSED", cs);
			else
			{
				LogMsg(LOG_LEVEL_ERROR, FLAG_REGS_QS_GATE, "\nCS%d: QS GATE TRAINING FAILED", cs);
                                ret_val = -3;
			}
		}
		LogMsg(LOG_LEVEL_INFO, FLAG_REGS_QS_GATE, "\nAfter QS gating:");
		logs_training_regs(QS_GATE);
	}
#endif

#ifdef DDR4_VREF_TRAINING
	/* Read PHY Vref Training - only for DDR4 */
	//Capture settings from TIM, update only if training passes.
        result->ddr4.vref_read = ll_read32(PHY_Control_15);
	if(tc_ddr_type == DDR4)
	{
		LogMsg(LOG_LEVEL_INFO, FLAG_REGS_VREF_READ, "\n\nVREF READ TRAINING\n===================");
		LogMsg(LOG_LEVEL_INFO, FLAG_REGS_VREF_READ, "\nBefore vref read training:");
		logs_training_regs(VREF_READ);
		vdac_value = vref_read_training(tc_cs_num, init_para);
		if (vdac_value >= 0) {/*training passed*/
			LogMsg(LOG_LEVEL_ERROR, FLAG_REGS_VREF_READ, "\nVREF READ TRAINING PASSED");
			LogMsg(LOG_LEVEL_INFO, FLAG_REGS_VREF_READ, "\nFinal vdac_value 0x%02X\n", vdac_value);
			vdac_set(1, vdac_value);/*Set the tuned vdac value*/
			result->ddr4.vref_read = ll_read32(PHY_Control_15);
		} else {
			LogMsg(LOG_LEVEL_ERROR, FLAG_REGS_VREF_READ, "\nVREF READ TRAINING FAILED");
			ret_val = -3;
		}
		LogMsg(LOG_LEVEL_INFO, FLAG_REGS_VREF_READ, "\nAfter vref read training:");
		logs_training_regs(VREF_READ);
	}

	/* Write DRAM Vref Training - only for DDR4 */
	//Capture settings from TIM, update only if training passes.
	result->ddr4.vref_write = ll_read32(CH0_DRAM_Config_4);
	if(tc_ddr_type == DDR4)
        {
		LogMsg(LOG_LEVEL_INFO, FLAG_REGS_VREF_WRITE, "\n\nVREF WRITE TRAINING\n===================");
                LogMsg(LOG_LEVEL_INFO, FLAG_REGS_VREF_WRITE, "\nBefore vref write training:");
		logs_training_regs(VREF_WRITE);
                vref_value = vref_write_training(tc_cs_num, init_para);
                if(vref_value != 0)				//training passed
		{
			LogMsg(LOG_LEVEL_ERROR, FLAG_REGS_VREF_WRITE, "\nVREF WRITE TRAINING PASSED");
                        LogMsg(LOG_LEVEL_INFO, FLAG_REGS_VREF_WRITE, "\nFinal vref_value 0x%08X\n", vref_value);
			en_dis_write_vref(1);
	                vref_set(1, vref_value);                //Set the tuned vref value
	                en_dis_write_vref(0);
			result->ddr4.vref_write = ll_read32(CH0_DRAM_Config_4);
                }
		else
                {
			LogMsg(LOG_LEVEL_ERROR, FLAG_REGS_VREF_WRITE, "\nVREF WRITE TRAINING FAILED");
			ret_val = -3;
		}
		LogMsg(LOG_LEVEL_INFO, FLAG_REGS_VREF_WRITE, "\nAfter vref write training:");
		logs_training_regs(VREF_WRITE);
        }
#endif

	/* 10. DLL tuning */
	//Capture settings from TIM, update only if training passes.
	result->dll_tune.dll_ctrl_b0 = ll_read32(CH0_PHY_DLL_control_B0);
	result->dll_tune.dll_ctrl_b1 = ll_read32(CH0_PHY_DLL_control_B1);
        result->dll_tune.dll_ctrl_adcm = ll_read32(CH0_PHY_DLL_control_ADCM);
	LogMsg(LOG_LEVEL_INFO, FLAG_REGS_DLL_TUNE, "\n\nDLL TUNING\n==============");
	LogMsg(LOG_LEVEL_INFO, FLAG_REGS_DLL_TUNE, "\nBefore DLL tuning:");
	logs_training_regs(DLL_TUNE);
	dll_res = dll_tuning(2, tc_cs_num, &init_para, false, true);
	if (dll_res > 0) {
		result->dll_tune.dll_ctrl_b0 =
			ll_read32(CH0_PHY_DLL_control_B0);
		result->dll_tune.dll_ctrl_b1 =
			ll_read32(CH0_PHY_DLL_control_B1);
		result->dll_tune.dll_ctrl_adcm =
			ll_read32(CH0_PHY_DLL_control_ADCM);
		LogMsg(LOG_LEVEL_ERROR, FLAG_REGS_DLL_TUNE, "\nDLL TUNING PASSED\n");
	} else {
		LogMsg(LOG_LEVEL_ERROR, FLAG_REGS_DLL_TUNE, "\nDLL TUNING FAILED\n");
		ret_val = -3;
	}
	LogMsg(LOG_LEVEL_INFO, FLAG_REGS_DLL_TUNE, "\nAfter DLL tuning:");
	logs_training_regs(DLL_TUNE);

	/* Test DRAM */
	//ddr_test_dma(0x1000); 	//TODO: size, base addr

	/* Summary of registers after training */
	LogMsg(LOG_LEVEL_INFO, FLAG_REGS_DUMP_ALL, "\n\nSummary of registers\n=================");
	LogMsg(LOG_LEVEL_INFO, FLAG_REGS_DUMP_ALL, "\n\tCH0_PHY_Control_6[0x%08X]: 0x%08X", CH0_PHY_Control_6, ll_read32(CH0_PHY_Control_6));
        LogMsg(LOG_LEVEL_INFO, FLAG_REGS_DUMP_ALL, "\n\tPHY_Control_15[0x%08X]: 0x%08X", PHY_Control_15, ll_read32(PHY_Control_15));
        LogMsg(LOG_LEVEL_INFO, FLAG_REGS_DUMP_ALL, "\n\tPHY_Control_16[0x%08X]: 0x%08X", PHY_Control_16, ll_read32(PHY_Control_16));
        LogMsg(LOG_LEVEL_INFO, FLAG_REGS_DUMP_ALL, "\n\tCH0_DRAM_Config_1[0x%08X]: 0x%08X", CH0_Dram_Config_1, ll_read32(CH0_Dram_Config_1));
        LogMsg(LOG_LEVEL_INFO, FLAG_REGS_DUMP_ALL, "\n\tCH0_PHY_Control_2[0x%08X]: 0x%08X", CH0_PHY_Control_2, ll_read32(CH0_PHY_Control_2));
	if(tc_ddr_type == DDR3)
	{
		LogMsg(LOG_LEVEL_INFO, FLAG_REGS_DUMP_ALL, "\n\tCH0_PHY_RL_Control_CS0_B0[0x%08X]: 0x%08X", CH0_PHY_RL_Control_CS0_B0, ll_read32(CH0_PHY_RL_Control_CS0_B0));
        	LogMsg(LOG_LEVEL_INFO, FLAG_REGS_DUMP_ALL, "\n\tCH0_PHY_RL_Control_CS0_B1[0x%08X]: 0x%08X", CH0_PHY_RL_Control_CS0_B1, ll_read32(CH0_PHY_RL_Control_CS0_B1));
        	LogMsg(LOG_LEVEL_INFO, FLAG_REGS_DUMP_ALL, "\n\tCH0_PHY_RL_Control_CS1_B0[0x%08X]: 0x%08X", CH0_PHY_RL_Control_CS1_B0, ll_read32(CH0_PHY_RL_Control_CS1_B0));
	        LogMsg(LOG_LEVEL_INFO, FLAG_REGS_DUMP_ALL, "\n\tCH0_PHY_RL_Control_CS1_B1[0x%08X]: 0x%08X\n", CH0_PHY_RL_Control_CS1_B1, ll_read32(CH0_PHY_RL_Control_CS1_B1));
	}
	else if(tc_ddr_type == DDR4)
	{
		LogMsg(LOG_LEVEL_INFO, FLAG_REGS_DUMP_ALL, "\n\tPHY_Control_15[0x%08X]: 0x%08X\n", PHY_Control_15, ll_read32(PHY_Control_15));
	        LogMsg(LOG_LEVEL_INFO, FLAG_REGS_DUMP_ALL, "\n\tCH0_DRAM_Config_4[0x%08X]: 0x%08X\n", CH0_DRAM_Config_4, ll_read32(CH0_DRAM_Config_4));
	}
	LogMsg(LOG_LEVEL_INFO, FLAG_REGS_DUMP_ALL, "\n\tCH0_PHY_DLL_control_B0[0x%08X]: 0x%08X", CH0_PHY_DLL_control_B0, ll_read32(CH0_PHY_DLL_control_B0));
        LogMsg(LOG_LEVEL_INFO, FLAG_REGS_DUMP_ALL, "\n\tCH0_PHY_DLL_control_B1[0x%08X]: 0x%08X", CH0_PHY_DLL_control_B1, ll_read32(CH0_PHY_DLL_control_B1));
        LogMsg(LOG_LEVEL_INFO, FLAG_REGS_DUMP_ALL, "\n\tCH0_PHY_DLL_control_ADCM[0x%08X]: 0x%08X\n", CH0_PHY_DLL_control_ADCM, ll_read32(CH0_PHY_DLL_control_ADCM));

        /* write and read Sanity check */
        if (!init_para.warm_boot)
                for(cs=0; cs<tc_cs_num; cs++)
                        self_refresh_test(1, init_para.cs_wins[cs].base, 1024);

#ifdef VALIDATION_EYE
	rx_sweep_test();
#endif
	return ret_val;
}
#ifdef VALIDATION_EYE
static int cpu_test(int byte_mask)
{
	int res = 0;
	int *uiSdramOffset = (int *)0x60000000;
	int i, pattern_end;

	pattern_end = sizeof(pattern) / sizeof(pattern[0]);


	for (i = 0; i < pattern_end; i++)
		(*uiSdramOffset++) = pattern[i];


	uiSdramOffset = (int *)0x60000000;
	for (i = 0; i < pattern_end; i++)
		if (BYTE_ERROR1((*uiSdramOffset++),
				pattern[i], byte_mask))
			res++;


	uiSdramOffset = (int *)0x61000000;
	for (i = 0; i < pattern_end; i++)
		(*uiSdramOffset++) = pattern[i];

	uiSdramOffset = (int *)0x61000000;
	for (i = 0; i < pattern_end; i++)
		if (BYTE_ERROR1((*uiSdramOffset++),
				pattern[i], byte_mask))
			res++;

	uiSdramOffset = (int *)0xa0000000;
	for (i = 0; i < pattern_end; i++)
		(*uiSdramOffset++) = pattern[i];

	uiSdramOffset = (int *)0xa0000000;
	for (i = 0; i < pattern_end; i++)
		if (BYTE_ERROR1((*uiSdramOffset++),
				pattern[i], byte_mask))
			res++;

	uiSdramOffset = (int *)0xa1000000;
	for (i = 0; i < pattern_end; i++)
		(*uiSdramOffset++) = pattern[i];

	uiSdramOffset = (int *)0xa1000000;
	for (i = 0; i < pattern_end; i++)
		if (BYTE_ERROR1((*uiSdramOffset++),
				pattern[i], byte_mask))
			res++;

	return res;

}
void set_adll(unsigned int addr, unsigned int data, unsigned int offset)
{
	int mask;
	unsigned int w32RegVal;
	int data_val;

	mask = 0x3f << offset;
	data_val =  data << offset;
	data_val &= mask;
	w32RegVal = ll_read32(addr);
	w32RegVal &= ~mask;
	w32RegVal |= data_val;
	ll_write32(addr, w32RegVal);
}

void rx_sweep_test(void)
{
	int  byte, addr, adll, res, vref, loop, offset, range,
		m_b0, n_b0, m_b1, n_b1, byte_mask;

	printf("&&&&&&&&&&& RX SWEEP &&&&&&&&&&&&&&\n");
	int vref_default = ll_read32(PHY_Control_15);

	int vref_val =  (vref_default & 0x3F000000) >> 24;

	int adll_byte0_default = ll_read32(0xc0001050);

	m_b0 = (adll_byte0_default & 0x3F0000) >> 16;
	n_b0 = (adll_byte0_default & 0x3F000000) >> 24;
	int adll_byte1_default = ll_read32(0xc0001054);

	m_b1 = (adll_byte1_default & 0x3F0000) >> 16;
	n_b1 = (adll_byte1_default & 0x3F000000) >> 24;

	printf("m_b0: 0x%x n_b0: 0x%x m_b1: 0x%x n_b1: 0x%x\n"
		, m_b0, n_b0, m_b1, n_b1);

	for (byte = 0; byte < 2; byte++) {
		printf("########### byte : %d ###############\n", byte);
		addr = (byte == 0) ? 0xc0001050 : 0xc0001054;
		byte_mask = (byte == 0) ? 0x00ff00ff : 0xff00ff00;
		for (loop = 0; loop < 2 ; loop++) {
			printf("########### loop : %d ###############\n"
				, loop);
			offset = (loop == 0) ? 16 : 24;
			for (vref = 0x3f; vref > 0 ; vref--) {
				range = 1;
				vdac_set(range, vref);
				for (adll = 0; adll < 64; adll++) {
					res = 0;
					set_adll(addr, adll, offset);
					res = cpu_test(byte_mask);
					if ((range == 1) &&
						(vref == vref_val)) {
						if (adll == m_b0)
							res = -1;
						else if (adll == n_b0)
							res = -2;
						else if (adll == m_b1)
							res = -3;
						else if (adll == n_b1)
							res = -4;
					}
					printf("%d, ", res);
				}
				printf("\n");
			}

			/*restore defaults*/
			ll_write32(0xc0001050, adll_byte0_default);
			ll_write32(0xc0001054, adll_byte1_default);
			ll_write32(PHY_Control_15, vref_default);
		}
		printf("\n");
	}
}
#endif
