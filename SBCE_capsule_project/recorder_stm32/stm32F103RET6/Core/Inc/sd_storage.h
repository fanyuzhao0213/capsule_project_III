/**
 ******************************************************************************
 * @file    sd_storage.h
 * @copyright Copyright (c) 2026
 * @author  Capsule Project Team
 * @brief   SD 卡 FatFs 测试及应用数据存储接口。
 * @version 1.0.0
 ******************************************************************************
 */
#ifndef SD_STORAGE_H
#define SD_STORAGE_H

#include <stdint.h>

/** @brief SD 存储模块操作状态。 */
typedef enum
{
  SD_STORAGE_OK = 0,
  SD_STORAGE_NOT_READY,
  SD_STORAGE_INVALID_PARAMETER,
  SD_STORAGE_MOUNT_ERROR,
  SD_STORAGE_OPEN_ERROR,
  SD_STORAGE_READ_ERROR,
  SD_STORAGE_WRITE_ERROR,
  SD_STORAGE_VERIFY_ERROR,
  SD_STORAGE_SYNC_ERROR
} SdStorageStatus_t;

/**
 * @brief  挂载 SD 卡文件系统并输出容量信息。
 * @param  None.
 * @return SD_STORAGE_OK 表示挂载成功，否则返回对应错误状态。
 * @note   非线程安全；必须在 MX_FATFS_Init() 之后调用。
 */
SdStorageStatus_t SdStorage_Init(void);

/**
 * @brief  卸载 SD 卡文件系统。
 * @param  None.
 * @return None.
 * @note   非线程安全；调用前必须确保没有文件处于打开状态。
 */
void SdStorage_DeInit(void);

/**
 * @brief  执行创建、写入、回读和内容校验测试。
 * @param  None.
 * @return SD_STORAGE_OK 表示全部测试通过，否则返回对应错误状态。
 * @note   非线程安全；测试会创建或覆盖 SD_TEST.TXT 文件。
 */
SdStorageStatus_t SdStorage_RunSelfTest(void);

/**
 * @brief  向指定文件末尾追加二进制数据并立即同步到 SD 卡。
 * @param  filename 根目录下的文件名，例如 "LPYD-01.CSV"。
 * @param  data 待写入的数据指针。
 * @param  length 待写入的字节数。
 * @return SD_STORAGE_OK 表示写入成功，否则返回对应错误状态。
 * @note   非线程安全且不可重入；只允许在主循环中调用。
 */
SdStorageStatus_t SdStorage_AppendFile(const char *filename,
                                       const uint8_t *data,
                                       uint16_t length);

/**
 * @brief  创建或覆盖一个文件并写入完整二进制内容。
 * @param  filename 根目录下的文件名。
 * @param  data 待写入的数据指针。
 * @param  length 待写入的字节数。
 * @return SD_STORAGE_OK 表示写入成功，否则返回对应错误状态。
 * @note   非线程安全且不可重入；只允许在主循环中调用。
 */
SdStorageStatus_t SdStorage_WriteFile(const char *filename,
                                      const uint8_t *data,
                                      uint16_t length);

/**
 * @brief  向 LPYD-01.CSV 文件末尾追加一条数据。
 * @param  data 待写入的数据指针。
 * @param  length 待写入的字节数。
 * @return SD_STORAGE_OK 表示写入成功，否则返回对应错误状态。
 * @note   非线程安全且不可重入；只允许在主循环中调用。
 */
SdStorageStatus_t SdStorage_AppendLpyd01(const uint8_t *data, uint16_t length);

/**
 * @brief  查询 SD 卡文件系统是否已经成功挂载。
 * @param  None.
 * @return 已挂载返回 1，否则返回 0。
 * @note   可重入；返回值仅表示最近一次挂载操作的状态。
 */
uint8_t SdStorage_IsReady(void);

/**
 * @brief Append one fixed-size image record to the capsule-specific .YS file.
 * @param filename Root filename generated from the complete 8-byte capsule SN.
 * @param record Exactly 20064 bytes: 128-byte DeviceInfo plus JPEG and padding.
 * @param length Must be 20064.
 * @param sync_now Nonzero flushes FatFs after this record (normally every 10).
 * @note The file has no global header. Its size is always N * 20064 bytes.
 */
SdStorageStatus_t SdStorage_AppendCapsuleRecord(const char *filename,
                                                const uint8_t *record,
                                                uint16_t length,
                                                uint8_t sync_now);

#endif /* SD_STORAGE_H */
