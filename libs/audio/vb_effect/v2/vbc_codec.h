/*
 * Copyright (C) 2012 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef __VBC_CODEC_H
#define __VBC_CODEC_H

#define LOG_TAG  "vbc_effect"


/* #include <asm/io.h> */
#include <cutils/log.h>


#define printk   ALOGW

#define VBC_FIFO_FRAME_NUM	  160
#define VBC_NOSIE_CURRENT_SOUND_HARDWARE_BUG_FIX 1

#define VBC_RATE_8000   (10)
#define VBC_RATE_9600   ( 9)
#define VBC_RATE_11025  ( 8)
#define VBC_RATE_12000  ( 7)
#define VBC_RATE_16000  ( 6)
#define VBC_RATE_22050  ( 5)
#define VBC_RATE_24000  ( 4)
#define VBC_RATE_32000  ( 3)
#define VBC_RATE_44100  ( 2)
#define VBC_RATE_48000  ( 1)
#define VBC_RATE_96000  ( 0)

/* VBADBUFFDTA */
enum {
	VBISADCK_INV = 9,
	VBISDACK_INV,
	VBLSB_EB,
	VBIIS_DLOOP = 13,
	VBPCM_MODE,
	VBIIS_LRCK,
};
/* VBDABUFFDTA */
enum {
	RAMSW_NUMB = 9,
	RAMSW_EN,
	VBAD0DMA_EN,
	VBAD1DMA_EN,
	VBDA0DMA_EN,
	VBDA1DMA_EN,
	VBENABLE,
};
/* VBAICR */
#define VBCAICR_MODE_ADC_I2S	(1 << 0)
#define VBCAICR_MODE_DAC_I2S	(1 << 1)
#define VBCAICR_MODE_ADC_SERIAL	(1 << 2)
#define VBCAICR_MODE_DAC_SERIAL	(1 << 3)
/* VBCR1 */
enum {
	BTL_MUTE = 1,
	BYPASS,
	DACSEL,
	HP_DIS,
	DAC_MUTE,
	MONO,
	SB_MICBIAS,
};
/* VBCR2 */
#define DAC_DATA_WIDTH_16_bit	(0x00)
#define DAC_DATA_WIDTH_18_bit	(0x01)
#define DAC_DATA_WIDTH_20_bit	(0x02)
#define DAC_DATA_WIDTH_24_bit	(0x03)

#define ADC_DATA_WIDTH_16_bit	(0x00)
#define ADC_DATA_WIDTH_18_bit	(0x01)
#define ADC_DATA_WIDTH_20_bit	(0x02)
#define ADC_DATA_WIDTH_24_bit	(0x03)

#define MICROPHONE1				(0)
#define MICROPHONE2				(1)
enum {
	MICSEL = 1,
	ADC_HPF,
	ADC_ADWL,
	DAC_ADWL = 5,
	DAC_DEEMP = 7,
};
/* VBPMR1 */
enum {
	SB_LOUT = 1,
	SB_BTL,
	SB_LIN,
	SB_ADC,
	SB_MIX,
	SB_OUT,
	SB_DAC,
};
/* VBPMR2 */
enum {
	SB_SLEEP = 0,
	SB,
	SB_MC,
	GIM,
	RLGOD,
	LRGOD,
};
/* -------------------------- */
#define SPRD_VBC_ALSA_CTRL2ARM_REG (SPRD_GREG_BASE + GR_BUSCLK)

#define SPRD_VB_BASE 0x0000000   //FIXME

#define ARM_VB_BASE		SPRD_VB_BASE

#define VIRT_VBDA0		(SPRD_VB_BASE + 0x0000) /* 0x0000  Voice band DAC0 data buffer */
#define VIRT_VBDA1		(SPRD_VB_BASE + 0x0004) /* 0x0002  Voice band DAC1 data buffer */
#define VIRT_VBAD0		(SPRD_VB_BASE + 0x0008) /* 0x0004  Voice band ADC0 data buffer */
#define VIRT_VBAD1		(SPRD_VB_BASE + 0x000C) /* 0x0006  Voice band ADC1 data buffer */
#define PHYS_VBDA0		(SPRD_VB_PHYS + 0x0000) /* 0x0000  Voice band DAC0 data buffer */
#define PHYS_VBDA1		(SPRD_VB_PHYS + 0x0004) /* 0x0002  Voice band DAC1 data buffer */
#define PHYS_VBAD0		(SPRD_VB_PHYS + 0x0008) /* 0x0004  Voice band ADC0 data buffer */
#define PHYS_VBAD1		(SPRD_VB_PHYS + 0x000C) /* 0x0006  Voice band ADC1 data buffer */

#define DMA_VB_DA0_BIT		(1 << DMA_VB_DA0)
#define DMA_VB_DA1_BIT		(1 << DMA_VB_DA1)
#define DMA_VB_AD0_BIT		(1 << DMA_VB_AD0)
#define DMA_VB_AD1_BIT		(1 << DMA_VB_AD1)

#define VBDA0			(ARM_VB_BASE + 0x0000) /* 0x0000  Voice band DAC0 data buffer */
#define VBDA1			(ARM_VB_BASE + 0x0004) /* 0x0002  Voice band DAC1 data buffer */
#define VBAD0			(ARM_VB_BASE + 0x0008) /* 0x0004  Voice band ADC0 data buffer */
#define VBAD1			(ARM_VB_BASE + 0x000C) /* 0x0006  Voice band ADC1 data buffer */
#define VBBUFFSIZE		(ARM_VB_BASE + 0x0010) /* 0x0008  Voice band buffer size */
#define VBADBUFFDTA		(ARM_VB_BASE + 0x0014) /* 0x000A  Voice band AD buffer control */
#define VBDABUFFDTA		(ARM_VB_BASE + 0x0018) /* 0x000C  Voice band DA buffer control */
#define VBADCNT			(ARM_VB_BASE + 0x001C) /* 0x000E  Voice band AD buffer counter */
#define VBDACNT			(ARM_VB_BASE + 0x0020) /* 0x0010  Voice band DA buffer counter */
#define VBDAICTL		(ARM_VB_BASE + 0x0024) /* 0x0012  Voice band DAI control */
#define VBDAIIN			(ARM_VB_BASE + 0x0028) /* 0x0014  Voice band DAI input */
#define VBDAIOUT		(ARM_VB_BASE + 0x002C) /* 0x0016  Voice band DAI output */

uint32_t VB_MAP_BASE ;
uint32_t VB_AD_CTL_MAP_BASE;




uint32_t DAPATCHCTL;
uint32_t DADGCTL   ;
uint32_t DAHPCTL   ;
uint32_t DAALCCTL0 ;
uint32_t DAALCCTL1 ;
uint32_t DAALCCTL2 ;
uint32_t DAALCCTL3 ;
uint32_t DAALCCTL4 ;
uint32_t DAALCCTL5 ;
uint32_t DAALCCTL6 ;
uint32_t DAALCCTL7 ;
uint32_t DAALCCTL8 ;
uint32_t DAALCCTL9 ;
uint32_t DAALCCTL10;
uint32_t STCTL0	   ;
uint32_t STCTL1	   ;
uint32_t DACSRCCTL ;
uint32_t MIXERCTL ;
uint32_t VBNGCVTHD ;
uint32_t VBNGCTTHD ;
uint32_t VBNGCTL ;
uint32_t ADPATCHCTL;
uint32_t ADDG01CTL   ;
uint32_t ADDG23CTL   ; 
uint32_t ADHPCTL   ; 
uint32_t HPCOEF0_H   ;            
uint32_t HPCOEF0_L  ;      
uint32_t HPCOEF1_H   ;            
uint32_t HPCOEF1_L  ;      
uint32_t HPCOEF2_H   ;            
uint32_t HPCOEF2_L  ;      
uint32_t HPCOEF3_H   ;            
uint32_t HPCOEF3_L  ;      
uint32_t HPCOEF4_H   ;            
uint32_t HPCOEF4_L  ;      
uint32_t HPCOEF5_H   ;            
uint32_t HPCOEF5_L  ;      
uint32_t HPCOEF6_H   ;            
uint32_t HPCOEF6_L  ;      
uint32_t HPCOEF7_H   ;            
uint32_t HPCOEF7_L  ;      
uint32_t HPCOEF8_H   ;            
uint32_t HPCOEF8_L  ;      
uint32_t HPCOEF9_H   ;            
uint32_t HPCOEF9_L  ;      
uint32_t HPCOEF10_H ;             
uint32_t HPCOEF10_L ;       
uint32_t HPCOEF11_H ;             
uint32_t HPCOEF11_L ;       
uint32_t HPCOEF12_H ;             
uint32_t HPCOEF12_L ;       
uint32_t HPCOEF13_H ;             
uint32_t HPCOEF13_L ;       
uint32_t HPCOEF14_H ;             
uint32_t HPCOEF14_L ;       
uint32_t HPCOEF15_H ;             
uint32_t HPCOEF15_L ;       
uint32_t HPCOEF16_H ;             
uint32_t HPCOEF16_L ;       
uint32_t HPCOEF17_H ;             
uint32_t HPCOEF17_L ;       
uint32_t HPCOEF18_H ;             
uint32_t HPCOEF18_L ;       
uint32_t HPCOEF19_H ;             
uint32_t HPCOEF19_L ;       
uint32_t HPCOEF20_H ;             
uint32_t HPCOEF20_L ;       
uint32_t HPCOEF21_H ;             
uint32_t HPCOEF21_L ;       
uint32_t HPCOEF22_H ;             
uint32_t HPCOEF22_L ;       
uint32_t HPCOEF23_H ;             
uint32_t HPCOEF23_L ;       
uint32_t HPCOEF24_H ;             
uint32_t HPCOEF24_L ;       
uint32_t HPCOEF25_H ;             
uint32_t HPCOEF25_L ;       
uint32_t HPCOEF26_H ;             
uint32_t HPCOEF26_L ;       
uint32_t HPCOEF27_H ;             
uint32_t HPCOEF27_L ;       
uint32_t HPCOEF28_H ;             
uint32_t HPCOEF28_L ;       
uint32_t HPCOEF29_H ;             
uint32_t HPCOEF29_L ;       
uint32_t HPCOEF30_H ;             
uint32_t HPCOEF30_L ;       
uint32_t HPCOEF31_H ;             
uint32_t HPCOEF31_L ;       
uint32_t HPCOEF32_H ;             
uint32_t HPCOEF32_L ;       
uint32_t HPCOEF33_H ;             
uint32_t HPCOEF33_L ;       
uint32_t HPCOEF34_H ;             
uint32_t HPCOEF34_L ;       
uint32_t HPCOEF35_H ;             
uint32_t HPCOEF35_L ;       
uint32_t HPCOEF36_H ;             
uint32_t HPCOEF36_L ;       
uint32_t HPCOEF37_H ;             
uint32_t HPCOEF37_L ;       
uint32_t HPCOEF38_H ;             
uint32_t HPCOEF38_L ;       
uint32_t HPCOEF39_H ;             
uint32_t HPCOEF39_L ;       
uint32_t HPCOEF40_H ;             
uint32_t HPCOEF40_L ;       
uint32_t HPCOEF41_H ;            
uint32_t HPCOEF41_L ;       
uint32_t HPCOEF42_H ;             
uint32_t HPCOEF42_L ;       
uint32_t HPCOEF43_H ;             
uint32_t HPCOEF43_L ;       
uint32_t HPCOEF44_H ;             
uint32_t HPCOEF44_L ;       
uint32_t HPCOEF45_H ;             
uint32_t HPCOEF45_L ;       
uint32_t HPCOEF46_H ;             
uint32_t HPCOEF46_L ;       
uint32_t HPCOEF47_H ;             
uint32_t HPCOEF47_L ;       
uint32_t HPCOEF48_H ;             
uint32_t HPCOEF48_L ;       
uint32_t HPCOEF49_H ;             
uint32_t HPCOEF49_L ;       
uint32_t HPCOEF50_H ;             
uint32_t HPCOEF50_L ;       
uint32_t HPCOEF51_H ;             
uint32_t HPCOEF51_L ;       
uint32_t HPCOEF52_H ;             
uint32_t HPCOEF52_L ;       
uint32_t HPCOEF53_H ;             
uint32_t HPCOEF53_L ;       
uint32_t HPCOEF54_H ;             
uint32_t HPCOEF54_L ;       
uint32_t HPCOEF55_H ;             
uint32_t HPCOEF55_L ;       
uint32_t HPCOEF56_H ;             
uint32_t HPCOEF56_L ;       
uint32_t HPCOEF57_H ;             
uint32_t HPCOEF57_L ;       
uint32_t HPCOEF58_H ;             
uint32_t HPCOEF58_L ;       
uint32_t HPCOEF59_H ;             
uint32_t HPCOEF59_L ;       
uint32_t HPCOEF60_H ;             
uint32_t HPCOEF60_L ;       
uint32_t HPCOEF61_H ;             
uint32_t HPCOEF61_L ;       
uint32_t HPCOEF62_H ;             
uint32_t HPCOEF62_L ;       
uint32_t HPCOEF63_H ;             
uint32_t HPCOEF63_L ;       
uint32_t HPCOEF64_H ;             
uint32_t HPCOEF64_L ;       
uint32_t HPCOEF65_H ;             
uint32_t HPCOEF65_L ;       
uint32_t HPCOEF66_H ;             
uint32_t HPCOEF66_L ;       
uint32_t HPCOEF67_H ;             
uint32_t HPCOEF67_L ;       
uint32_t HPCOEF68_H ;             
uint32_t HPCOEF68_L ;       
uint32_t HPCOEF69_H ;             
uint32_t HPCOEF69_L ;       
uint32_t HPCOEF70_H ;             
uint32_t HPCOEF70_L ;       
uint32_t HPCOEF71_H ;             
uint32_t HPCOEF71_L ;       
uint32_t ADC01_HPCOEF0_H   ;      
uint32_t ADC01_HPCOEF0_L   ;
uint32_t ADC01_HPCOEF1_H   ;      
uint32_t ADC01_HPCOEF1_L   ;
uint32_t ADC01_HPCOEF2_H   ;      
uint32_t ADC01_HPCOEF2_L   ;
uint32_t ADC01_HPCOEF3_H   ;      
uint32_t ADC01_HPCOEF3_L   ;
uint32_t ADC01_HPCOEF4_H   ;      
uint32_t ADC01_HPCOEF4_L   ;
uint32_t ADC01_HPCOEF5_H   ;      
uint32_t ADC01_HPCOEF5_L   ;
uint32_t ADC01_HPCOEF6_H   ;      
uint32_t ADC01_HPCOEF6_L   ;
uint32_t ADC01_HPCOEF7_H   ;      
uint32_t ADC01_HPCOEF7_L   ;
uint32_t ADC01_HPCOEF8_H   ;      
uint32_t ADC01_HPCOEF8_L   ;
uint32_t ADC01_HPCOEF9_H   ;      
uint32_t ADC01_HPCOEF9_L   ;
uint32_t ADC01_HPCOEF10_H ;       
uint32_t ADC01_HPCOEF10_L ; 
uint32_t ADC01_HPCOEF11_H ;       
uint32_t ADC01_HPCOEF11_L ; 
uint32_t ADC01_HPCOEF12_H ;       
uint32_t ADC01_HPCOEF12_L ; 
uint32_t ADC01_HPCOEF13_H ;       
uint32_t ADC01_HPCOEF13_L ; 
uint32_t ADC01_HPCOEF14_H ;       
uint32_t ADC01_HPCOEF14_L ; 
uint32_t ADC01_HPCOEF15_H ;       
uint32_t ADC01_HPCOEF15_L ; 
uint32_t ADC01_HPCOEF16_H ;       
uint32_t ADC01_HPCOEF16_L ; 
uint32_t ADC01_HPCOEF17_H ;       
uint32_t ADC01_HPCOEF17_L ; 
uint32_t ADC01_HPCOEF18_H ;       
uint32_t ADC01_HPCOEF18_L ; 
uint32_t ADC01_HPCOEF19_H ;       
uint32_t ADC01_HPCOEF19_L ; 
uint32_t ADC01_HPCOEF20_H ;       
uint32_t ADC01_HPCOEF20_L ; 
uint32_t ADC01_HPCOEF21_H ;       
uint32_t ADC01_HPCOEF21_L ; 
uint32_t ADC01_HPCOEF22_H ;       
uint32_t ADC01_HPCOEF22_L ; 
uint32_t ADC01_HPCOEF23_H ;       
uint32_t ADC01_HPCOEF23_L ; 
uint32_t ADC01_HPCOEF24_H ;       
uint32_t ADC01_HPCOEF24_L ; 
uint32_t ADC01_HPCOEF25_H ;       
uint32_t ADC01_HPCOEF25_L ; 
uint32_t ADC01_HPCOEF26_H ;       
uint32_t ADC01_HPCOEF26_L ; 
uint32_t ADC01_HPCOEF27_H ;       
uint32_t ADC01_HPCOEF27_L ; 
uint32_t ADC01_HPCOEF28_H ;       
uint32_t ADC01_HPCOEF28_L ; 
uint32_t ADC01_HPCOEF29_H ;       
uint32_t ADC01_HPCOEF29_L ; 
uint32_t ADC01_HPCOEF30_H ;       
uint32_t ADC01_HPCOEF30_L ; 
uint32_t ADC01_HPCOEF31_H ;       
uint32_t ADC01_HPCOEF31_L ; 
uint32_t ADC01_HPCOEF32_H ;       
uint32_t ADC01_HPCOEF32_L ; 
uint32_t ADC01_HPCOEF33_H ;       
uint32_t ADC01_HPCOEF33_L ; 
uint32_t ADC01_HPCOEF34_H ;       
uint32_t ADC01_HPCOEF34_L ; 
uint32_t ADC01_HPCOEF35_H ;       
uint32_t ADC01_HPCOEF35_L ; 
uint32_t ADC01_HPCOEF36_H ;       
uint32_t ADC01_HPCOEF36_L ; 
uint32_t ADC01_HPCOEF37_H ;       
uint32_t ADC01_HPCOEF37_L ; 
uint32_t ADC01_HPCOEF38_H ;       
uint32_t ADC01_HPCOEF38_L ; 
uint32_t ADC01_HPCOEF39_H ;       
uint32_t ADC01_HPCOEF39_L ; 
uint32_t ADC01_HPCOEF40_H ;       
uint32_t ADC01_HPCOEF40_L ; 
uint32_t ADC01_HPCOEF41_H ;       
uint32_t ADC01_HPCOEF41_L ; 
uint32_t ADC01_HPCOEF42_H ;       
uint32_t ADC01_HPCOEF42_L ; 
uint32_t ADC23_HPCOEF0_H   ;      
uint32_t ADC23_HPCOEF0_L   ;
uint32_t ADC23_HPCOEF1_H   ;      
uint32_t ADC23_HPCOEF1_L   ;
uint32_t ADC23_HPCOEF2_H   ;      
uint32_t ADC23_HPCOEF2_L   ;
uint32_t ADC23_HPCOEF3_H   ;      
uint32_t ADC23_HPCOEF3_L   ;
uint32_t ADC23_HPCOEF4_H   ;      
uint32_t ADC23_HPCOEF4_L   ;
uint32_t ADC23_HPCOEF5_H   ;      
uint32_t ADC23_HPCOEF5_L   ;
uint32_t ADC23_HPCOEF6_H   ;      
uint32_t ADC23_HPCOEF6_L   ;
uint32_t ADC23_HPCOEF7_H   ;      
uint32_t ADC23_HPCOEF7_L   ;
uint32_t ADC23_HPCOEF8_H   ;      
uint32_t ADC23_HPCOEF8_L   ;
uint32_t ADC23_HPCOEF9_H   ;      
uint32_t ADC23_HPCOEF9_L   ;
uint32_t ADC23_HPCOEF10_H ;       
uint32_t ADC23_HPCOEF10_L ; 
uint32_t ADC23_HPCOEF11_H ;       
uint32_t ADC23_HPCOEF11_L ; 
uint32_t ADC23_HPCOEF12_H ;       
uint32_t ADC23_HPCOEF12_L ; 
uint32_t ADC23_HPCOEF13_H ;       
uint32_t ADC23_HPCOEF13_L ; 
uint32_t ADC23_HPCOEF14_H ;       
uint32_t ADC23_HPCOEF14_L ; 
uint32_t ADC23_HPCOEF15_H ;       
uint32_t ADC23_HPCOEF15_L ; 
uint32_t ADC23_HPCOEF16_H ;       
uint32_t ADC23_HPCOEF16_L ; 
uint32_t ADC23_HPCOEF17_H ;       
uint32_t ADC23_HPCOEF17_L ; 
uint32_t ADC23_HPCOEF18_H ;       
uint32_t ADC23_HPCOEF18_L ; 
uint32_t ADC23_HPCOEF19_H ;       
uint32_t ADC23_HPCOEF19_L ; 
uint32_t ADC23_HPCOEF20_H ;       
uint32_t ADC23_HPCOEF20_L ; 
uint32_t ADC23_HPCOEF21_H ;       
uint32_t ADC23_HPCOEF21_L ; 
uint32_t ADC23_HPCOEF22_H ;       
uint32_t ADC23_HPCOEF22_L ; 
uint32_t ADC23_HPCOEF23_H ;       
uint32_t ADC23_HPCOEF23_L ; 
uint32_t ADC23_HPCOEF24_H ;       
uint32_t ADC23_HPCOEF24_L ; 
uint32_t ADC23_HPCOEF25_H ;       
uint32_t ADC23_HPCOEF25_L ; 
uint32_t ADC23_HPCOEF26_H ;       
uint32_t ADC23_HPCOEF26_L ; 
uint32_t ADC23_HPCOEF27_H ;       
uint32_t ADC23_HPCOEF27_L ; 
uint32_t ADC23_HPCOEF28_H ;       
uint32_t ADC23_HPCOEF28_L ; 
uint32_t ADC23_HPCOEF29_H ;       
uint32_t ADC23_HPCOEF29_L ; 
uint32_t ADC23_HPCOEF30_H ;       
uint32_t ADC23_HPCOEF30_L ; 
uint32_t ADC23_HPCOEF31_H ;       
uint32_t ADC23_HPCOEF31_L ; 
uint32_t ADC23_HPCOEF32_H ;       
uint32_t ADC23_HPCOEF32_L ; 
uint32_t ADC23_HPCOEF33_H ;       
uint32_t ADC23_HPCOEF33_L ; 
uint32_t ADC23_HPCOEF34_H ;       
uint32_t ADC23_HPCOEF34_L ; 
uint32_t ADC23_HPCOEF35_H ;       
uint32_t ADC23_HPCOEF35_L ; 
uint32_t ADC23_HPCOEF36_H ;       
uint32_t ADC23_HPCOEF36_L ; 
uint32_t ADC23_HPCOEF37_H ;       
uint32_t ADC23_HPCOEF37_L ; 
uint32_t ADC23_HPCOEF38_H ;       
uint32_t ADC23_HPCOEF38_L ; 
uint32_t ADC23_HPCOEF39_H ;       
uint32_t ADC23_HPCOEF39_L ; 
uint32_t ADC23_HPCOEF40_H ;       
uint32_t ADC23_HPCOEF40_L ; 
uint32_t ADC23_HPCOEF41_H ;       
uint32_t ADC23_HPCOEF41_L ; 
uint32_t ADC23_HPCOEF42_H ;       
uint32_t ADC23_HPCOEF42_L ; 
/* sc8810 ldo register */
#define	LDO_REG_BASE		(SPRD_MISC_BASE + 0x600)
#define	ANA_LDO_PD_CTL0		(LDO_REG_BASE  + 0x10)
#define ANA_AUDIO_CTRL		(LDO_REG_BASE  + 0x74)
#define	ANA_AUDIO_PA_CTRL0	(LDO_REG_BASE  + 0x78)
#define	ANA_AUDIO_PA_CTRL1	(LDO_REG_BASE  + 0x7C)

#ifndef SPRD_ADI_BASE
#define SPRD_ADI_BASE           SPRD_MISC_BASE
#endif
#ifndef SPRD_ADI_PHYS
#define SPRD_ADI_PHYS           SPRD_MISC_PHYS
#endif

#define ANA_REG_ADDR_START      (SPRD_MISC_BASE + 0x40)  /* 0x82000040 */
#define ANA_REG_ADDR_END        (SPRD_MISC_BASE + 0x780) /* 0x82000780 */

#define ARM_VB_BASE2		SPRD_MISC_BASE

#define VBAICR			(ARM_VB_BASE2 + 0x0100) /* 0x0080 Voice band Codec AICR */
#define VBCR1			(ARM_VB_BASE2 + 0x0104) /* 0x0082 Voice band Codec CR1 */
#define VBCR2			(ARM_VB_BASE2 + 0x0108) /* 0x0084 Voice band Codec CR2 */
#define VBCCR1			(ARM_VB_BASE2 + 0x010C) /* 0x0086 Voice band Codec CCR1 */
#define VBCCR2			(ARM_VB_BASE2 + 0x0110) /* 0x0088 Voice band Codec CCR2 */
#define VBPMR1			(ARM_VB_BASE2 + 0x0114) /* 0x008A Voice band Codec PMR1 */
#define VBPMR2			(ARM_VB_BASE2 + 0x0118) /* 0x008C Voice band Codec PMR2 */
#define VBCRR			(ARM_VB_BASE2 + 0x011C) /* 0x008E Voice band Codec CRR */
#define VBICR			(ARM_VB_BASE2 + 0x0120) /* 0x0090 Voice band Codec ICR */
#define VBIFR			(ARM_VB_BASE2 + 0x0124) /* 0x0092 Voice band Codec IFR */
#define VBCGR1			(ARM_VB_BASE2 + 0x0128) /* 0x0094 Voice band Codec CGR1 */
#define VBCGR2			(ARM_VB_BASE2 + 0x012C) /* 0x0096 Voice band Codec CGR2 */
#define VBCGR3			(ARM_VB_BASE2 + 0x0130) /* 0x0098 Voice band Codec CGR3 */
#define VBCGR8			(ARM_VB_BASE2 + 0x0144) /* 0x00A2 Voice band Codec CGR8 */
#define VBCGR9			(ARM_VB_BASE2 + 0x0148) /* 0x00A4 Voice band Codec CGR9 */
#define VBCGR10			(ARM_VB_BASE2 + 0x014C) /* 0x00A6 Voice band Codec CGR10 */
#define VBTR1			(ARM_VB_BASE2 + 0x0150) /* 0x00A8 Voice band Codec TR1 */
#define VBTR2			(ARM_VB_BASE2 + 0x0154) /* 0x00AA Voice band Codec TR2 */


/* -------------------------- */
extern uint32_t vbc_reg_write(uint32_t reg, uint8_t shift, uint32_t val, uint32_t mask);
extern uint32_t vbc_reg_read(uint32_t reg, uint8_t shift, uint32_t mask);

#endif
