/*
****************************************************************************
* Copyright(C): TY Technical
* FileName    : dev_cx93510_regs.h
* Author      : TY Technical Software Development Team
* Description : 摄像头cx93510寄存器定义
****************************************************************************
*/

#ifndef _DEV_CX93510_REGS_H_
#define _DEV_CX93510_REGS_H_



/**************************************************************/
/* Sensor Interface					      */
/**************************************************************/
#define     SI_CFG_1            0xA0

/* Reserved [7:6] */
#define     FLD_POL_SVREF       0x20
#define     FLD_POL_SHREF       0x10
#define     FLD_B_ORD           0x0C
#define     FLD_Y_ONLY          0x02
#define     FLD_CHRM_OFF        0x01
/**************************************************************/
#define     SI_CFG_2            0xA1

#define     FLD_EN_LFC          0x80
#define     FLD_EN_CFC          0x40
#define     FLD_SCLK_EN         0x40
#define     FLD_EMBD_CDE_ERR    0x10
#define     FLD_FRAME_SKIP      0x0F

/**************************************************************/
#define     SI_CFG_3            0xA2

/* Reserved [7] */
#define     FLD_EN_EMBD_CDE     0x40
#define     FLD_FRAME_NUM       0x3F
/**************************************************************/
#define     H_ACT               0xA3

#define     FLD_H_ACT           0xFF

/**************************************************************/
#define     H_CAP_DLY           0xA4

#define     FLD_H_CAP_DLY       0xFF
/**************************************************************/
#define     H_CAP_WIDTH         0xA5

/* Reserved [7] */
#define     FLD_H_CAP_WIDTH     0x7F

/**************************************************************/
#define     V_ACT               0xA6

#define     FLD_V_ACT           0xFF

/**************************************************************/
#define     V_CAP_DLY           0xA7

#define     FLD_V_CAP_DLY       0xFF
/**************************************************************/
#define     V_CAP_HEIGHT        0xA8

/* Reserved [7:6] */
#define     FLD_V_CAP_HEIGHT    0x3F

/**************************************************************/
/*  I2C					      		      */ 
/**************************************************************/
#define     I2C_DADDR           0xA9

#define     FLD_DADDR           0xFE
#define     FLD_PWDN_MODE       0x01
/**************************************************************/
#define     I2C_LO_ADDR         0xAA

#define     FLD_I2C_LO_ADDR     0xFF
/**************************************************************/
#define     I2C_HI_ADDR         0xAB

#define     FLD_I2C_HI_ADDR     0xFF
/**************************************************************/
#define     I2C_LO_DATA         0xAC

#define     FLD_I2C_LO_DATA     0xFF
/**************************************************************/
#define     I2C_HI_DATA         0xAD

#define     FLD_I2C_HI_DATA     0xFF

/**************************************************************/
#define     I2C_CTL_1           0xAE

#define     FLD_I2C_PERIOD      0xFF
/**************************************************************/
#define     I2C_CTL_2           0xAF

#define     FLD_I2C_SADDR_LEN   0xC0
#define     FLD_I2C_SYNC        0x20
#define     FLD_I2C_READ_SA     0x10
#define     FLD_I2C_PCS         0x08
#define     FLD_I2C_SADDR_INC   0x04
#define     FLD_I2C_2_EN        0x02
#define     FLD_I2C_SCCB_EN     0x01
/**************************************************************/
#define     I2C_CTL_3           0xB0

#define     FLD_I2C_DATA_16     0x80
#define     FLD_SCCB_E_EN       0x40
#define     FLD_I2C_READ_WRN    0x20
#define     FLD_SI_CFG_FIFO_CNT 0x1C
#define     FLD_I2C_RACK        0x02
#define     FLD_XFER_IN_PROG    0x01

/**************************************************************/
/* Filter Config					      */
/**************************************************************/
#define     FILTER_CFG          0xB1

/* Reserved [7:4] */
#define     FLD_COEFF           0x03
#define     FLD_EN_BLACK        0x02
#define     FLD_EN_FILTER       0x01

/**************************************************************/
/* JPEG controller					      */
/**************************************************************/
#define     DIFF_JPEG_CTRL      0x20

#define     FLD_RST_COMP        0x80
#define     FLD_FRM_BUF_FULL    0x40
#define     FLD_CONT_MODE       0x20
#define     FLD_REF_FREQ        0x17
#define     FLD_DIFF_EN         0x01


/**************************************************************/
/* JPEG Encoder status					      */
/**************************************************************/
#define     JPEG_ENC_STAT       0x21

#define     FLD_ENC_HF_ERROR    0x80
#define     FLD_CTL_ERROR       0x40
#define     FLD_HT_ERROR        0x20
#define     FLD_QT_ERROR        0x10
#define     FLD_ENC_ERR         0x08
#define     FLD_PIX_IN_PROG     0x04
#define     FLD_ENC_IN_PROG     0x02
#define     FLD_JPG_IN_PROG     0x01
/**************************************************************/
#define     JPEG_ENC_STAT2      0x22

/* Reserved [7:4] */
#define     FLD_FULL_LM         0x04
#define     FLD_FULL_CB         0x02
#define     FLD_FULL_CR         0x01
/**************************************************************/
#define     JPEG_ENC_PVAL_TYPE  0x23

/* Reserved [7:5] */
#define     FLD_ENC_PVALID      0x10
#define     FLD_ENC_PTYPE       0x0F
/**************************************************************/
#define     JPEG_ENC_PVALUE1    0x24

#define     FLD_PVALUE_MSB      0xFF
/**************************************************************/
#define     JPEG_ENC_PVALUE2    0x25

#define     FLD_PVALUE_LSB      0xFF
/**************************************************************/
#define     JPEG_ENC_CTL        0x26

/* Reserved [7:6] */
#define     FLD_JPG_MASK_REG    0x3E
#define     FLD_RELOAD_TABLES   0x01
/**************************************************************/
#define     JPEG_ENC_DCT_LM     0x27

#define     FLD_DCT_TAB_LM      0xFF
/**************************************************************/
#define     JPEG_ENC_DCT_CH     0x28

#define     FLD_DCT_TAB_CH      0xFF

/**************************************************************/
/* JPEG Decoder					      	      */
/**************************************************************/
#define     JPEG_DEC_STAT1      0x29

#define     FLD_DEC_HT_ERR      0x80
#define     FLD_CTL_ERR         0x40
#define     FLD_HT_ERR          0x20
#define     FLD_QT_ERR          0x01
#define     FLD_DEC_ERR         0x08
#define     FLD_IDCT_IN_PROG    0x04
#define     FLD_DEC_IN_PROG     0x02
#define     FLD_JPG_IN_PROG     0x01
/**************************************************************/
#define     JPEG_DEC_STAT2      0x2A

#define     FLD_FETCH_ERR       0x80
#define     FLD_PVALID          0x40
#define     FLD_SIGSOS          0x20
#define     FLD_INIT_PROG       0x10
#define     FLD_DEC_PTYPE       0x0F
/**************************************************************/
#define     JPEG_DEC_STAT3      0x2B

/* Reserved [7:5] */
#define     FLD_DEC_PROC_ERR    0x10
#define     FLD_DEC_PROC_REF    0x08
#define     FLD_DEC_PROC_CFG    0x04
/* Reserved [1] */
#define     FLD_INIT_DONE       0x01
/**************************************************************/
#define     JPEG_DEC_PVALUE1    0x2D

#define     FLD_DEC_PVALUE_MSB  0xFF
/**************************************************************/
#define     JPEG_DEC_PVALUE2    0x2D

#define     FLD_DEC_PVALUE_LSB  0xFF
/**************************************************************/
#define     JPEG_DEC_TBLDEF_DBG 0x2E

#define     FLD_TBLDEF          0xFF


/**************************************************************/
/* Memory 					      	      */
/**************************************************************/
#define     MEM_ADJ             0x30

/* Reserved [7:6] */
#define     FLD_MEM_EMA_COMP    0x38
#define     FLD_MEM_EMA_FB      0x07
/**************************************************************/
#define     MEM_TEST_CNTL       0x31

/* Reserved [7:6] */
#define     FLD_FB_RETEN_TST_GO     0x20
#define     FLD_SENS_MBIST_GO       0x10
#define     FLD_COMP_DP_MBIST_GO    0x08
#define     FLD_COMP_SP_MBIST_GO    0x04
#define     FLD_FB_MBIST_GO         0x02
#define     FLD_MEM_BIST_EN         0x01
/**************************************************************/
#define     MEM_TEST_STAT1          0x32

/* Reserved [7] */
#define     FLD_SENS_MBIST_FAIL     0x40
#define     FLD_SENS_MBIST_TST_DONE 0x20
#define     FLD_FB_MBIST_FAIL       0x1E
#define     FLD_FB_MBIST_TST_DONE   0x01
/**************************************************************/
#define     MEM_TEST_STAT2          0x33

/* Reserved [7] */
#define     FLD_COMP_SP_MBIST_FAIL     0x7E
#define     FLD_COMP_SP_MBIST_TST_DONE 0x01
/**************************************************************/
#define     MEM_TEST_STAT3          0x34

/* Reserved [7:6] */
#define     FLD_COMP_DP_MBIST_FAIL     0x3E
#define     FLD_COMP_DP_MBIST_TST_DONE 0x01
/**************************************************************/
#define     MEM_TEST_STAT4          0x34

/* Reserved [7:5] */
#define     FLD_FB_RETEN_TST_FAIL   0x1E
#define     FLD_FB_RETEN_TST_DONE   0x01


/**************************************************************/
/*  Host Interface 					      */
/**************************************************************/
#define     SLAVE_SEL_CTRL          0x50

/* Reserved [7:3] */
#define     FLD_U_FLW_CTRL          0x04
#define     FLD_SLAVE_SELECT        0x03

/**************************************************************/
#define     FB_ADDR_0               0x51

/* Reserved [7] */
#define     FLD_FB_ADDR_0           0x7F
/**************************************************************/
#define     FB_ADDR_1               0x52

/* Reserved [7] */
#define     FLD_FB_ADDR_1           0xFF
/**************************************************************/
#define     FB_ADDR_2               0x53

/* Reserved [7:6] */
#define     FLD_PREFETCH_DONE       0x20
#define     FLD_TRANSACTION         0x18
#define     FLD_QWORD_BYTE_PTR      0x07

/**************************************************************/
#define     ERR_STAT                0x54

/* Reserved [7:4] */
#define     FLD_WR_FREQ_ERR         0x08
#define     FLD_VID_REQ_ERR         0x04
#define     FLD_FIFO_UNDERRUN       0x02
#define     FLD_GNT_ERR             0x01
/**************************************************************/
#define     HWR_FB_EN               0x55

/* Reserved [7:5] */
#define     FLD_AUD_DEBUG           0x10
#define     FLD_RAW_AUD             0x08
#define     FLD_AUD_RESTART         0x04
#define     FLD_AUDIO_ENABLE        0x02
#define     FLD_HOST_WRITE_ENABLE   0x01

/**************************************************************/
/* Write/Read Data from/to Host				      */
/**************************************************************/
#define     HWDATA_FB               0x56

#define     FLD_HWDATA_FB           0xFF
/**************************************************************/
#define     HRDATA_FB               0xCC

#define     FLD_HRDATA_FB           0xFF


/**************************************************************/
// Audio Buffer Status
/**************************************************************/
#define     AUD_BUFF_STAT           0x57

/* Reserved [7:3] */
#define     FLD_AUD_EMPTY           0x04
#define     FLD_AUD_FULL            0x02
#define     FLD_AUD_HALF_FULL       0x01
/**************************************************************/
#define     AUD_RD_PTR_0            0x58

/* Reserved [7] */
#define     FLD_AUD_RD_PTR_0        0x7F
/**************************************************************/
#define     AUD_RD_PTR_1            0x59

#define     FLD_AUD_RD_PTR_1        0xFF

/**************************************************************/
#define     AUD_QWORDS_0            0x5A

#define     FLD_AUD_QWORDS_0        0xFF
/**************************************************************/
#define     AUD_QWORDS_1            0x5B

/* Reserved [7:2] */
#define     FLD_AUD_QWORDS_1        0x03


/**************************************************************/
/* ADPCM samples 					      */
/**************************************************************/
#define     ADPCM_N_0               0x5C

#define     FLD_ADPCM_N_0           0xFF
/**************************************************************/
#define     ADPCM_N_1               0x5D

/* Reserved [7:2] */
#define     FLD_ADPCM_N_1           0x03
/**************************************************************/
#define     ADPCM                   0x5E

/* Reserved [7:1] */
#define     FLD_ADPCM_MODE          0x01


/**************************************************************/
/* Photo Cell					      	      */
/**************************************************************/
#define     PHCELL_OUT_0            0x5F

#define     FLD_PHCELL_OUT_0        0xFF
/**************************************************************/
#define     PHCELL_OUT_1            0x60

#define     FLD_PHCELL_OUT_1        0xFF


/**************************************************************/
/* GPIO					      		      */
/**************************************************************/
#define     GPIN                    0xF0

#define     FLD_GPIN                0xFF
/**************************************************************/
#define     GPOUT                   0xF1

#define     FLD_GPOUT               0xFF
/**************************************************************/
#define     GPOE                    0xF2

#define     FLD_GPOE                0xFF

/**************************************************************/
#define     PN_BO_REV               0xFF

#define     FLD_PID                 0xF8
#define     FLD_BO                  0x0C
#define     FLD_REV                 0x03


/**************************************************************/
/*  AFE					      		      */
/**************************************************************/
#define     ADC1                0x00

#define     FLD_PDB_MICADC      0x80
#define     FLD_MIC_GAIN        0x70
#define     FLD_PDB_MICBIAS     0x08
/* Reserved [2:1] */
#define     FLD_SEL_AUXADC      0x01
/**************************************************************/
#define     ADC2                0x01

#define     FLD_CTRL_ADC_IDAC1  0xC0
#define     FLD_CTRL_ADC_IDAC2  0x30
#define     FLD_ADC_IOP         0x0C
#define     FLD_ADC_AGND        0x03


/**************************************************************/
/*  Misc. controls					      */
/**************************************************************/

/**************************************************************/
#define     LED1_3              0x02

#define     FLD_PDB_LED         0x80
#define     FLD_CTRL_LED        0x7C
#define     FLD_HI_GAIN_LED     0x02
#define     FLD_DIS_LIM         0x01
/**************************************************************/

#define     LDO_ANA             0x03
#define     LDO_DIG             0x04
#define     RET_LDO_DIG         0x05
#define     BGREF               0x06
#define     IBIAS               0x07
#define     ANATEST1            0x08
#define     ANATEST2            0x09

/**************************************************************/
#define     ADC_CTRL_DIG1       0x0A

/* Reserved [7:4] */
#define     FLD_MIC_SAMPLE_RATE 0x08
#define     FLD_MIC_ADC_GAIN    0x06
#define     FLD_MIC_ADC_EN      0x01
/**************************************************************/
#define     ADC_CTRL_DIG2       0x0B

/* Reserved [7:3] */
#define     FLD_AUX_ADC_GAIN    0x06
#define     FLD_AUX_ADC_EN      0x01
/**************************************************************/
#define     ADC_CTRL_DIG3       0x0C
#define     ADC_CTRL_DIG4       0x0D
#define     LED_PWM_CTRL        0x0E
#define     LED_ON_DELAY        0x0F



#endif /* _DEV_CX93510_REGS_H_ */

