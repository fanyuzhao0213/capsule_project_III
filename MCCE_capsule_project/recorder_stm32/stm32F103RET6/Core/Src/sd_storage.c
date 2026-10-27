/**
 ******************************************************************************
 * @file    sd_storage.c
 * @copyright Copyright (c) 2026
 * @author  Capsule Project Team
 * @brief   SD 卡 FatFs 测试及应用数据存储实现。
 * @version 1.0.0
 ******************************************************************************
 */

#include "sd_storage.h"
#include "app_log.h"
#include "fatfs.h"
#include "bsp_driver_sd.h"
#include "iwdg.h"
#include "sdio.h"
#include <stdio.h>
#include <string.h>

#define SD_STORAGE_TEST_FILENAME    "SD_TEST.TXT" /**< SD 卡自检文件名。 */
#define SD_STORAGE_LPYD01_FILENAME  "LPYD-01.CSV" /**< LPYD-01 数据文件名。 */
#define SD_STORAGE_PATH_SIZE        80U            /**< 完整路径缓冲区大小。 */
#define SD_STORAGE_CAPSULE_RECORD_SIZE  20064U

/** @brief 自检时写入并回读的固定内容。 */
static const uint8_t sd_test_data[] =
    "STM32 SDIO + FatFs read/write test passed.\r\n";

/** @brief 文件系统成功挂载标志。 */
static uint8_t sd_storage_ready;
static FIL sd_capsule_file;
static uint8_t sd_capsule_file_open;
static char sd_capsule_filename[32];

/**
 * @brief  根据逻辑盘符和文件名生成完整路径。
 * @param  path 保存完整路径的目标缓冲区。
 * @param  path_size 目标缓冲区字节数。
 * @param  filename 根目录下的文件名。
 * @return 成功返回 1，参数错误或空间不足返回 0。
 * @note   不可重入；调用者必须提供独立的 path 缓冲区。
 */
static uint8_t SdStorage_BuildPath(char *path, uint16_t path_size,
                                   const char *filename)
{
  int result;

  if ((path == 0) || (filename == 0) || (path_size == 0U))
  {
    return 0U;
  }
  result = snprintf(path, path_size, "%s%s", SDPath, filename);
  return ((result >= 0) && ((uint16_t)result < path_size)) ? 1U : 0U;
}

/**
 * @brief  输出 FatFs 错误码。
 * @param  operation 发生错误的操作名称。
 * @param  result FatFs 返回的错误码。
 * @return None.
 * @note   非线程安全；内部使用阻塞日志，只允许在主循环中调用。
 */
static void SdStorage_LogError(const char *operation, FRESULT result)
{
  LOG_ERROR("SD %s failed, FatFs error=%u", operation, (unsigned int)result);
  LOG_ERROR("SD diagnostic: HAL state=%u card=%u error=0x%08lX",
            (unsigned int)HAL_SD_GetState(&hsd),
            (unsigned int)HAL_SD_GetCardState(&hsd),
            (unsigned long)HAL_SD_GetError(&hsd));
}

/**
 * @brief 作废当前胶囊文件并恢复SDIO/FatFs，确保下一帧可重新打开。
 *
 * 简易流程图：
 *   清除FIL和当前文件名
 *       → 卸载FatFs卷
 *       → HAL_SD_Abort停止异常传输
 *       → BSP_SD_Init重新初始化SD卡
 *       → 重新挂载FatFs
 *       ├─ 成功：storage_ready=1
 *       └─ 失败：保持不可用并记录诊断
 */
static void SdStorage_InvalidateCapsuleFile(void)
{
  HAL_StatusTypeDef abort_status;
  uint8_t init_status;
  FRESULT mount_status;

  /* f_write失败后不再对旧FIL执行f_sync/f_close，但必须卸载卷来清除
   * FatFs的文件共享锁，否则下一次f_open会持续返回FR_LOCKED(16)。 */
  sd_capsule_file_open = 0U;
  sd_capsule_filename[0] = '\0';
  (void)memset(&sd_capsule_file, 0, sizeof(sd_capsule_file));
  (void)f_mount(0, SDPath, 0U);
  sd_storage_ready = 0U;

  /* 传输错误后卡可能停在SENDING/RECEIVING状态；先STOP再初始化回4-bit。 */
  abort_status = HAL_SD_Abort(&hsd);
  init_status = BSP_SD_Init();
  if (init_status == MSD_OK)
  {
    mount_status = f_mount(&SDFatFS, SDPath, 1U);
    if (mount_status == FR_OK)
    {
      sd_storage_ready = 1U;
      LOG_WARNING("SD transfer recovered: abort=%u reinit=OK remount=OK",
                  (unsigned int)abort_status);
    }
    else
    {
      LOG_ERROR("SD transfer remount failed: abort=%u FatFs=%u",
                (unsigned int)abort_status, (unsigned int)mount_status);
    }
  }
  else
  {
    LOG_ERROR("SD transfer recovery failed: abort=%u reinit=%u error=0x%08lX",
              (unsigned int)abort_status, (unsigned int)init_status,
              (unsigned long)HAL_SD_GetError(&hsd));
  }
}

/**
 * @brief  将 HAL SD 卡类型转换为日志文本。
 * @param  card_type HAL 返回的卡类型编码。
 * @return 指向静态卡类型字符串的指针。
 * @note   可重入；返回字符串为只读静态常量。
 */
static const char *SdStorage_GetCardTypeText(uint32_t card_type)
{
  if (card_type == CARD_SDSC)
  {
    return "SDSC";
  }
  if (card_type == CARD_SDHC_SDXC)
  {
    return "SDHC/SDXC";
  }
  if (card_type == CARD_SECURED)
  {
    return "SECURED";
  }
  return "UNKNOWN";
}

/**
 * @brief  将 FatFs 文件系统类型转换为日志文本。
 * @param  fs_type FatFs 文件系统类型编码。
 * @return 指向静态文件系统类型字符串的指针。
 * @note   可重入；返回字符串为只读静态常量。
 */
static const char *SdStorage_GetFatTypeText(uint8_t fs_type)
{
  if (fs_type == FS_FAT12)
  {
    return "FAT12";
  }
  if (fs_type == FS_FAT16)
  {
    return "FAT16";
  }
  if (fs_type == FS_FAT32)
  {
    return "FAT32";
  }
  return "UNKNOWN";
}

/**
 * @brief  通过 UART1 打印 SD 卡、SDIO 和文件系统信息。
 * @param  fs 已经成功挂载的 FatFs 对象指针。
 * @param  total_sectors 文件系统总扇区数。
 * @param  free_sectors 文件系统剩余扇区数。
 * @return None.
 * @note   非线程安全；内部使用阻塞式日志，只允许在主循环中调用。
 */
static void SdStorage_PrintCardInfo(const FATFS *fs, DWORD total_sectors,
                                    DWORD free_sectors)
{
  HAL_SD_CardInfoTypeDef card_info;
  uint32_t clock_div;
  uint32_t clock_hz;
  uint32_t bus_width;

  if ((fs == 0) || (HAL_SD_GetCardInfo(&hsd, &card_info) != HAL_OK))
  {
    LOG_WARNING("SD card information unavailable");
    return;
  }

  clock_div = SDIO->CLKCR & SDIO_CLKCR_CLKDIV; /* CLKDIV 位保存当前硬件分频值。 */
  clock_hz = HAL_RCC_GetHCLKFreq() / (clock_div + 2U);
  bus_width = SDIO->CLKCR & SDIO_CLKCR_WIDBUS; /* WIDBUS 位表示当前实际总线宽度。 */

  LOG_INFO("---------- SD card information ----------");
  LOG_INFO("Drive       : %s", SDPath);
  LOG_INFO("Card type   : %s", SdStorage_GetCardTypeText(card_info.CardType));
  LOG_INFO("Card version: %s",
           (card_info.CardVersion == CARD_V2_X) ? "SD v2.x" : "SD v1.x");
  LOG_INFO("Card class  : 0x%08lX", (unsigned long)card_info.Class);
  LOG_INFO("RCA         : 0x%04lX", (unsigned long)card_info.RelCardAdd);
  LOG_INFO("Block size  : %lu bytes", (unsigned long)card_info.LogBlockSize);
  LOG_INFO("Block count : %lu", (unsigned long)card_info.LogBlockNbr);
  LOG_INFO("Capacity    : %lu MB",
           (unsigned long)(((uint64_t)card_info.LogBlockNbr *
                            card_info.LogBlockSize) / (1024ULL * 1024ULL)));
  LOG_INFO("File system : %s", SdStorage_GetFatTypeText(fs->fs_type));
  LOG_INFO("Cluster size: %lu bytes",
           (unsigned long)((uint32_t)fs->csize * 512U));
  LOG_INFO("Total space : %lu MB", (unsigned long)(total_sectors / 2048U));
  LOG_INFO("Free space  : %lu MB", (unsigned long)(free_sectors / 2048U));
  LOG_INFO("SDIO bus    : %s",
           (bus_width == SDIO_BUS_WIDE_4B) ? "4-bit" : "1-bit");
  LOG_INFO("SDIO clock  : %lu kHz", (unsigned long)(clock_hz / 1000U));
  LOG_INFO("-----------------------------------------");
}

/**
 * @brief  挂载 SD 卡文件系统并输出容量信息。
 * @param  None.
 * @return SD_STORAGE_OK 表示挂载成功，否则返回对应错误状态。
 * @note   非线程安全；必须在 MX_FATFS_Init() 之后调用。
 *
 * 简易流程图：
 *   检查FatFs驱动是否成功链接
 *       → 喂狗并立即挂载逻辑盘
 *       → 标记SD可用
 *       → 查询总容量/剩余容量
 *       → 打印卡类型、FAT类型、总线宽度和时钟
 *       → 再次喂狗并返回
 */
SdStorageStatus_t SdStorage_Init(void)
{
  FRESULT result;
  FATFS *fs;
  DWORD free_clusters;
  DWORD total_sectors;
  DWORD free_sectors;

  sd_storage_ready = 0U;
  if (retSD != 0U)
  {
    LOG_ERROR("SD FatFs driver link failed, result=%u", (unsigned int)retSD);
    return SD_STORAGE_MOUNT_ERROR;
  }

  (void)HAL_IWDG_Refresh(&hiwdg);
  result = f_mount(&SDFatFS, SDPath, 1U);
  if (result != FR_OK)
  {
    SdStorage_LogError("mount", result);
    LOG_ERROR("SD HAL state=%u, error=0x%08lX",
              (unsigned int)HAL_SD_GetState(&hsd),
              (unsigned long)HAL_SD_GetError(&hsd));
    return SD_STORAGE_MOUNT_ERROR;
  }
  sd_storage_ready = 1U;

  result = f_getfree(SDPath, &free_clusters, &fs);
  if (result == FR_OK)
  {
    total_sectors = (fs->n_fatent - 2U) * fs->csize;
    free_sectors = free_clusters * fs->csize;
    SdStorage_PrintCardInfo(fs, total_sectors, free_sectors);
  }
  else
  {
    SdStorage_LogError("get free space", result);
  }
  (void)HAL_IWDG_Refresh(&hiwdg);
  return SD_STORAGE_OK;
}

/**
 * @brief  卸载 SD 卡文件系统。
 * @param  None.
 * @return None.
 * @note   非线程安全；调用前必须确保没有文件处于打开状态。
 */
void SdStorage_DeInit(void)
{
  if (sd_capsule_file_open != 0U)
  {
    (void)f_sync(&sd_capsule_file);
    (void)f_close(&sd_capsule_file);
    sd_capsule_file_open = 0U;
    sd_capsule_filename[0] = '\0';
  }
  (void)f_mount(0, SDPath, 0U);
  sd_storage_ready = 0U;
}

/**
 * @brief  执行创建、写入、回读和内容校验测试。
 * @param  None.
 * @return SD_STORAGE_OK 表示全部测试通过，否则返回对应错误状态。
 * @note   非线程安全；测试会创建或覆盖 SD_TEST.TXT 文件。
 */
SdStorageStatus_t SdStorage_RunSelfTest(void)
{
  FIL file;
  FRESULT result;
  UINT transferred;
  char path[SD_STORAGE_PATH_SIZE];
  uint8_t read_buffer[sizeof(sd_test_data)];

  if (sd_storage_ready == 0U)
  {
    return SD_STORAGE_NOT_READY;
  }
  if (SdStorage_BuildPath(path, sizeof(path), SD_STORAGE_TEST_FILENAME) == 0U)
  {
    return SD_STORAGE_INVALID_PARAMETER;
  }

  (void)HAL_IWDG_Refresh(&hiwdg);
  result = f_open(&file, path, FA_CREATE_ALWAYS | FA_WRITE);
  if (result != FR_OK)
  {
    SdStorage_LogError("test open for write", result);
    LOG_ERROR("SD write diagnostic: HAL state=%u, card state=%u, error=0x%08lX",
              (unsigned int)HAL_SD_GetState(&hsd),
              (unsigned int)HAL_SD_GetCardState(&hsd),
              (unsigned long)HAL_SD_GetError(&hsd));
    return SD_STORAGE_OPEN_ERROR;
  }
  result = f_write(&file, sd_test_data, sizeof(sd_test_data) - 1U, &transferred);
  if ((result != FR_OK) || (transferred != (sizeof(sd_test_data) - 1U)) ||
      (f_sync(&file) != FR_OK))
  {
    (void)f_close(&file);
    SdStorage_LogError("test write", result);
    return SD_STORAGE_WRITE_ERROR;
  }
  (void)f_close(&file);

  (void)HAL_IWDG_Refresh(&hiwdg);
  result = f_open(&file, path, FA_READ);
  if (result != FR_OK)
  {
    SdStorage_LogError("test open for read", result);
    return SD_STORAGE_OPEN_ERROR;
  }
  (void)memset(read_buffer, 0, sizeof(read_buffer));
  result = f_read(&file, read_buffer, sizeof(sd_test_data) - 1U, &transferred);
  (void)f_close(&file);
  if ((result != FR_OK) || (transferred != (sizeof(sd_test_data) - 1U)))
  {
    SdStorage_LogError("test read", result);
    return SD_STORAGE_READ_ERROR;
  }
  if (memcmp(read_buffer, sd_test_data, sizeof(sd_test_data) - 1U) != 0)
  {
    LOG_ERROR("SD test verify failed");
    return SD_STORAGE_VERIFY_ERROR;
  }

  (void)HAL_IWDG_Refresh(&hiwdg);
  LOG_INFO("SD read/write self-test passed: %s", path);
  return SD_STORAGE_OK;
}

/**
 * @brief  向指定文件末尾追加二进制数据并立即同步到 SD 卡。
 * @param  filename 根目录下的文件名。
 * @param  data 待写入的数据指针。
 * @param  length 待写入的字节数。
 * @return SD_STORAGE_OK 表示写入成功，否则返回对应错误状态。
 * @note   非线程安全且不可重入；只允许在主循环中调用。
 */
SdStorageStatus_t SdStorage_AppendFile(const char *filename,
                                       const uint8_t *data,
                                       uint16_t length)
{
  FIL file;
  FRESULT result;
  UINT written = 0U;
  char path[SD_STORAGE_PATH_SIZE];

  if (sd_storage_ready == 0U)
  {
    return SD_STORAGE_NOT_READY;
  }
  if ((filename == 0) || (data == 0) || (length == 0U) ||
      (SdStorage_BuildPath(path, sizeof(path), filename) == 0U))
  {
    return SD_STORAGE_INVALID_PARAMETER;
  }

  (void)HAL_IWDG_Refresh(&hiwdg);
  result = f_open(&file, path, FA_OPEN_ALWAYS | FA_WRITE);
  if (result != FR_OK)
  {
    SdStorage_LogError("append open", result);
    return SD_STORAGE_OPEN_ERROR;
  }
  result = f_lseek(&file, f_size(&file));
  if (result == FR_OK)
  {
    result = f_write(&file, data, length, &written);
  }
  if ((result == FR_OK) && (written == length))
  {
    result = f_sync(&file);
  }
  (void)f_close(&file);
  (void)HAL_IWDG_Refresh(&hiwdg);

  if ((result != FR_OK) || (written != length))
  {
    SdStorage_LogError("append write", result);
    return SD_STORAGE_WRITE_ERROR;
  }
  return SD_STORAGE_OK;
}

/**
 * @brief  创建或覆盖一个文件并写入完整二进制内容。
 * @param  filename 根目录下的文件名。
 * @param  data 待写入的数据指针。
 * @param  length 待写入的字节数。
 * @return SD_STORAGE_OK 表示写入成功，否则返回对应错误状态。
 * @note   非线程安全且不可重入；函数会在返回前同步并关闭文件。
 */
SdStorageStatus_t SdStorage_WriteFile(const char *filename,
                                      const uint8_t *data,
                                      uint16_t length)
{
  FIL file;
  FRESULT result;
  UINT written = 0U;
  char path[SD_STORAGE_PATH_SIZE];

  if (sd_storage_ready == 0U)
  {
    return SD_STORAGE_NOT_READY;
  }
  if ((filename == 0) || (data == 0) || (length == 0U) ||
      (SdStorage_BuildPath(path, sizeof(path), filename) == 0U))
  {
    return SD_STORAGE_INVALID_PARAMETER;
  }

  (void)HAL_IWDG_Refresh(&hiwdg);
  result = f_open(&file, path, FA_CREATE_ALWAYS | FA_WRITE);
  if (result != FR_OK)
  {
    SdStorage_LogError("image open", result);
    return SD_STORAGE_OPEN_ERROR;
  }
  result = f_write(&file, data, length, &written);
  if ((result == FR_OK) && (written == length))
  {
    result = f_sync(&file);
  }
  (void)f_close(&file);
  (void)HAL_IWDG_Refresh(&hiwdg);

  if ((result != FR_OK) || (written != length))
  {
    SdStorage_LogError("image write", result);
    return SD_STORAGE_WRITE_ERROR;
  }
  return SD_STORAGE_OK;
}

/**
 * @brief  向 LPYD-01.CSV 文件末尾追加一条数据。
 * @param  data 待写入的数据指针。
 * @param  length 待写入的字节数。
 * @return SD_STORAGE_OK 表示写入成功，否则返回对应错误状态。
 * @note   非线程安全且不可重入；只允许在主循环中调用。
 */
SdStorageStatus_t SdStorage_AppendLpyd01(const uint8_t *data, uint16_t length)
{
  return SdStorage_AppendFile(SD_STORAGE_LPYD01_FILENAME, data, length);
}

/**
 * @brief  查询 SD 卡文件系统是否已经成功挂载。
 * @param  None.
 * @return 已挂载返回 1，否则返回 0。
 * @note   可重入；返回值仅表示最近一次挂载操作的状态。
 */
uint8_t SdStorage_IsReady(void)
{
  return sd_storage_ready;
}

/**
 * @brief 打开或创建指定胶囊SN对应的.YS文件，准备追加固定记录。
 *
 * 简易流程图：
 *   当前已打开同名文件？→ 直接复用
 *       → 文件名变化：sync并关闭旧文件
 *       → 打开/创建Y<SN>.YS
 *       → 文件长度是20064整数倍？
 *       ├─ 否：截断掉电留下的不完整尾记录并sync
 *       └─ 是：保持原文件
 *       → 定位到文件末尾等待追加
 */
static SdStorageStatus_t SdStorage_OpenCapsuleFile(const char *filename)
{
  FRESULT result;
  char path[SD_STORAGE_PATH_SIZE];
  DWORD file_size;

  if ((sd_capsule_file_open != 0U) &&
      (strcmp(sd_capsule_filename, filename) == 0))
  {
    return SD_STORAGE_OK;
  }
  if (sd_capsule_file_open != 0U)
  {
    result = f_sync(&sd_capsule_file);
    (void)f_close(&sd_capsule_file);
    sd_capsule_file_open = 0U;
    if (result != FR_OK)
    {
      SdStorage_LogError("capsule switch sync", result);
      return SD_STORAGE_SYNC_ERROR;
    }
  }
  if (SdStorage_BuildPath(path, sizeof(path), filename) == 0U)
  {
    return SD_STORAGE_INVALID_PARAMETER;
  }

  result = f_open(&sd_capsule_file, path, FA_OPEN_ALWAYS | FA_WRITE);
  if (result != FR_OK)
  {
    SdStorage_LogError("capsule open", result);
    return SD_STORAGE_OPEN_ERROR;
  }
  sd_capsule_file_open = 1U;
  (void)strncpy(sd_capsule_filename, filename,
                sizeof(sd_capsule_filename) - 1U);
  sd_capsule_filename[sizeof(sd_capsule_filename) - 1U] = '\0';

  file_size = f_size(&sd_capsule_file);
  if ((file_size % SD_STORAGE_CAPSULE_RECORD_SIZE) != 0U)
  {
    DWORD valid_size = file_size -
        (file_size % SD_STORAGE_CAPSULE_RECORD_SIZE);
    LOG_WARNING("Capsule partial tail recovery: %s size=%lu truncate=%lu",
                filename, (unsigned long)file_size,
                (unsigned long)valid_size);
    result = f_lseek(&sd_capsule_file, valid_size);
    if (result == FR_OK)
    {
      result = f_truncate(&sd_capsule_file);
    }
    if (result == FR_OK)
    {
      result = f_sync(&sd_capsule_file);
    }
    if (result != FR_OK)
    {
      SdStorage_LogError("capsule tail recovery", result);
      SdStorage_InvalidateCapsuleFile();
      return SD_STORAGE_VERIFY_ERROR;
    }
    file_size = valid_size;
  }
  if (file_size == 0U)
  {
    LOG_INFO("Created capsule file: %s (no file header)", filename);
  }
  else
  {
    LOG_INFO("Continue capsule file: %s existing_records=%lu", filename,
             (unsigned long)(file_size / SD_STORAGE_CAPSULE_RECORD_SIZE));
  }
  result = f_lseek(&sd_capsule_file, file_size);
  if (result != FR_OK)
  {
    SdStorage_LogError("capsule seek", result);
    (void)f_close(&sd_capsule_file);
    sd_capsule_file_open = 0U;
    return SD_STORAGE_OPEN_ERROR;
  }
  return SD_STORAGE_OK;
}

/**
 * @brief 向胶囊.YS文件追加一条固定20064字节图片记录，并按策略同步磁盘。
 *
 * 简易流程图：
 *   检查SD状态、参数和固定长度
 *       → 打开或复用对应SN文件
 *       → 写入20064B记录
 *       → 写失败：废弃文件对象并重置SD链路
 *       → sync_now非0时执行f_sync
 *       → 喂狗并返回状态
 *
 * @note 上层当前每10帧置一次sync_now；未sync记录仍可能停留在FatFs缓存中。
 */
SdStorageStatus_t SdStorage_AppendCapsuleRecord(const char *filename,
                                                const uint8_t *record,
                                                uint16_t length,
                                                uint8_t sync_now)
{
  SdStorageStatus_t status;
  FRESULT result;
  UINT written = 0U;

  //检查 SD 是否准备好
  if (sd_storage_ready == 0U)
  {
    return SD_STORAGE_NOT_READY;
  }
  //检查参数
  if ((filename == 0) || (record == 0) ||
      (length != SD_STORAGE_CAPSULE_RECORD_SIZE))
  {
    return SD_STORAGE_INVALID_PARAMETER;
  }
  //打开目标文件
  status = SdStorage_OpenCapsuleFile(filename);
  if (status != SD_STORAGE_OK)
  {
    return status;
  }
  //写入前刷新看门狗
  (void)HAL_IWDG_Refresh(&hiwdg);
  //写入记录
  result = f_write(&sd_capsule_file, record, length, &written);
  //写入失败处理
  if ((result != FR_OK) || (written != length))
  {
    SdStorage_LogError("capsule record", result);
    /* FR_DISK_ERR会使FIL对象失效；下一帧必须重新打开。 */
    SdStorage_InvalidateCapsuleFile();
    return SD_STORAGE_WRITE_ERROR;
  }
  //根据 sync_now决定是否同步
  if (sync_now != 0U)
  {
    result = f_sync(&sd_capsule_file);
    if (result != FR_OK)
    {
      SdStorage_LogError("capsule sync", result);
      SdStorage_InvalidateCapsuleFile();
      return SD_STORAGE_SYNC_ERROR;
    }
  }
  (void)HAL_IWDG_Refresh(&hiwdg);
  return SD_STORAGE_OK;
}
