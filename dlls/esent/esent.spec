@ stub DebugExtensionInitialize
@ stub DebugExtensionNotify
@ stub DebugExtensionUninitialize
@ stub JetAddColumn
@ stub JetAddColumnA
@ stub JetAddColumnW
@ stub JetAttachDatabase2
@ stub JetAttachDatabase2A
@ stub JetAttachDatabase2W
@ stdcall JetAttachDatabase(ptr str long)
@ stub JetAttachDatabaseA
@ stub JetAttachDatabaseW
@ stub JetAttachDatabaseWithStreaming
@ stub JetAttachDatabaseWithStreamingA
@ stub JetAttachDatabaseWithStreamingW
@ stub JetBackup
@ stub JetBackupA
@ stdcall JetBackupInstance(ptr str long ptr)
@ stub JetBackupInstanceA
@ stub JetBackupInstanceW
@ stub JetBackupW
@ stub JetBeginDatabaseIncrementalReseed
@ stub JetBeginDatabaseIncrementalReseedA
@ stub JetBeginDatabaseIncrementalReseedW
@ stub JetBeginExternalBackup
@ stub JetBeginExternalBackupInstance
@ stdcall JetBeginSession(ptr ptr str str)
@ stub JetBeginSessionA
@ stub JetBeginSessionW
@ stub JetBeginSurrogateBackup
@ stdcall JetBeginTransaction2(ptr long)
@ stdcall JetBeginTransaction(ptr)
@ stdcall JetCloseDatabase(ptr long long)
@ stub JetCloseFile
@ stub JetCloseFileInstance
@ stdcall JetCloseTable(ptr ptr)
@ stdcall JetCommitTransaction(ptr long)
@ stub JetCompact
@ stub JetCompactA
@ stub JetCompactW
@ stub JetComputeStats
@ stub JetConfigureProcessForCrashDump
@ stub JetConvertDDL
@ stub JetConvertDDLA
@ stub JetConvertDDLW
@ stub JetCreateDatabase2
@ stub JetCreateDatabase2A
@ stub JetCreateDatabase2W
@ stdcall JetCreateDatabase(ptr str str ptr long)
@ stub JetCreateDatabaseA
@ stub JetCreateDatabaseW
@ stub JetCreateDatabaseWithStreaming
@ stub JetCreateDatabaseWithStreamingA
@ stub JetCreateDatabaseWithStreamingW
@ stdcall JetCreateIndex2(ptr ptr ptr long)
@ stub JetCreateIndex2A
@ stub JetCreateIndex2W
@ stub JetCreateIndex3A
@ stub JetCreateIndex3W
@ stub JetCreateIndex
@ stub JetCreateIndexA
@ stub JetCreateIndexW
@ stub JetCreateInstance2
@ stub JetCreateInstance2A
@ stub JetCreateInstance2W
@ stdcall JetCreateInstance(ptr str)
@ stub JetCreateInstanceA
@ stub JetCreateInstanceW
@ stub JetCreateTable
@ stub JetCreateTableA
@ stdcall JetCreateTableColumnIndex2(ptr long ptr)
@ stub JetCreateTableColumnIndex2A
@ stub JetCreateTableColumnIndex2W
@ stub JetCreateTableColumnIndex3A
@ stub JetCreateTableColumnIndex3W
@ stub JetCreateTableColumnIndex
@ stub JetCreateTableColumnIndexA
@ stub JetCreateTableColumnIndexW
@ stub JetCreateTableW
@ stub JetDBUtilities
@ stub JetDBUtilitiesA
@ stub JetDBUtilitiesW
@ stub JetDatabaseScan
@ stdcall JetDefragment2(ptr long str ptr ptr ptr long)
@ stub JetDefragment2A
@ stub JetDefragment2W
@ stub JetDefragment3
@ stub JetDefragment3A
@ stub JetDefragment3W
@ stdcall JetDefragment(ptr long str ptr ptr long)
@ stub JetDefragmentA
@ stub JetDefragmentW
@ stdcall JetDelete(ptr ptr)
@ stub JetDeleteColumn2
@ stub JetDeleteColumn2A
@ stub JetDeleteColumn2W
@ stub JetDeleteColumn
@ stub JetDeleteColumnA
@ stub JetDeleteColumnW
@ stub JetDeleteIndex
@ stub JetDeleteIndexA
@ stub JetDeleteIndexW
@ stub JetDeleteTable
@ stub JetDeleteTableA
@ stub JetDeleteTableW
@ stub JetDetachDatabase2
@ stub JetDetachDatabase2A
@ stub JetDetachDatabase2W
@ stdcall JetDetachDatabase(ptr str)
@ stub JetDetachDatabaseA
@ stub JetDetachDatabaseW
@ stub JetDupCursor
@ stub JetDupSession
@ stub JetEnableFaultInjection
@ stub JetEnableMultiInstance
@ stub JetEnableMultiInstanceA
@ stub JetEnableMultiInstanceW
@ stub JetEndDatabaseIncrementalReseed
@ stub JetEndDatabaseIncrementalReseedA
@ stub JetEndDatabaseIncrementalReseedW
@ stub JetEndExternalBackup
@ stub JetEndExternalBackupInstance2
@ stub JetEndExternalBackupInstance
@ stdcall JetEndSession(ptr long)
@ stub JetEndSurrogateBackup
@ stdcall JetEnumerateColumns(ptr ptr long ptr ptr ptr ptr ptr long long)
@ stub JetEscrowUpdate
@ stub JetExternalRestore2
@ stub JetExternalRestore2A
@ stub JetExternalRestore2W
@ stub JetExternalRestore
@ stub JetExternalRestoreA
@ stub JetExternalRestoreW
@ stub JetFreeBuffer
@ stub JetGetAttachInfo
@ stub JetGetAttachInfoA
@ stub JetGetAttachInfoInstance
@ stub JetGetAttachInfoInstanceA
@ stub JetGetAttachInfoInstanceW
@ stub JetGetAttachInfoW
@ stub JetGetBookmark
@ stdcall JetGetColumnInfo(ptr long str ptr ptr long long)
@ stub JetGetColumnInfoA
@ stub JetGetColumnInfoW
@ stub JetGetCounter
@ stub JetGetCurrentIndex
@ stub JetGetCurrentIndexA
@ stub JetGetCurrentIndexW
@ stub JetGetCursorInfo
@ stub JetGetDatabaseFileInfo
@ stub JetGetDatabaseFileInfoA
@ stub JetGetDatabaseFileInfoW
@ stdcall JetGetDatabaseInfo(ptr long ptr long long)
@ stub JetGetDatabaseInfoA
@ stub JetGetDatabaseInfoW
@ stub JetGetDatabasePages
@ stub JetGetIndexInfo
@ stub JetGetIndexInfoA
@ stub JetGetIndexInfoW
@ stub JetGetInstanceInfo
@ stub JetGetInstanceInfoA
@ stub JetGetInstanceInfoW
@ stub JetGetInstanceMiscInfo
@ stub JetGetLS
@ stub JetGetLock
@ stub JetGetLogFileInfo
@ stub JetGetLogFileInfoA
@ stub JetGetLogFileInfoW
@ stub JetGetLogInfo
@ stub JetGetLogInfoA
@ stub JetGetLogInfoInstance2
@ stub JetGetLogInfoInstance2A
@ stub JetGetLogInfoInstance2W
@ stub JetGetLogInfoInstance
@ stub JetGetLogInfoInstanceA
@ stub JetGetLogInfoInstanceW
@ stub JetGetLogInfoW
@ stub JetGetMaxDatabaseSize
@ stub JetGetObjectInfo
@ stub JetGetObjectInfoA
@ stub JetGetObjectInfoW
@ stub JetGetPageInfo2
@ stub JetGetPageInfo
@ stdcall JetGetRecordPosition(ptr ptr ptr long)
@ stub JetGetRecordSize2
@ stub JetGetRecordSize
@ stub JetGetResourceParam
@ stub JetGetSecondaryIndexBookmark
@ stub JetGetSessionInfo
@ stdcall JetGetSystemParameter(ptr ptr long ptr ptr long)
@ stub JetGetSystemParameterA
@ stub JetGetSystemParameterW
@ stdcall JetGetTableColumnInfo(ptr ptr ptr ptr long long)
@ stub JetGetTableColumnInfoA
@ stub JetGetTableColumnInfoW
@ stdcall JetGetTableIndexInfo(ptr ptr str ptr long long)
@ stub JetGetTableIndexInfoA
@ stub JetGetTableIndexInfoW
@ stub JetGetTableInfo
@ stub JetGetTableInfoA
@ stub JetGetTableInfoW
@ stub JetGetThreadStats
@ stub JetGetTruncateLogInfoInstance
@ stub JetGetTruncateLogInfoInstanceA
@ stub JetGetTruncateLogInfoInstanceW
@ stub JetGetVersion
@ stub JetGotoBookmark
@ stub JetGotoPosition
@ stub JetGotoSecondaryIndexBookmark
@ stdcall JetGrowDatabase(ptr long long ptr)
@ stdcall JetIdle(ptr long)
@ stub JetIndexRecordCount
@ stub JetInit2
@ stub JetInit3
@ stub JetInit3A
@ stub JetInit3W
@ stdcall JetInit(ptr)
@ stub JetIntersectIndexes
@ stdcall JetMakeKey(ptr ptr ptr long long)
@ stdcall JetMove(ptr ptr long long)
@ stub JetOSSnapshotAbort
@ stub JetOSSnapshotEnd
@ stub JetOSSnapshotFreeze
@ stub JetOSSnapshotFreezeA
@ stub JetOSSnapshotFreezeW
@ stub JetOSSnapshotGetFreezeInfo
@ stub JetOSSnapshotGetFreezeInfoA
@ stub JetOSSnapshotGetFreezeInfoW
@ stub JetOSSnapshotPrepare
@ stub JetOSSnapshotPrepareInstance
@ stub JetOSSnapshotThaw
@ stub JetOSSnapshotTruncateLog
@ stub JetOSSnapshotTruncateLogInstance
@ stdcall JetOpenDatabase(ptr str str ptr long)
@ stub JetOpenDatabaseA
@ stub JetOpenDatabaseW
@ stub JetOpenFile
@ stub JetOpenFileA
@ stub JetOpenFileInstance
@ stub JetOpenFileInstanceA
@ stub JetOpenFileInstanceW
@ stub JetOpenFileSectionInstance
@ stub JetOpenFileSectionInstanceA
@ stub JetOpenFileSectionInstanceW
@ stub JetOpenFileW
@ stdcall JetOpenTable(ptr long str ptr long long ptr)
@ stub JetOpenTableA
@ stub JetOpenTableW
@ stub JetOpenTempTable2
@ stub JetOpenTempTable3
@ stub JetOpenTempTable
@ stub JetOpenTemporaryTable
@ stub JetPatchDatabasePages
@ stub JetPatchDatabasePagesA
@ stub JetPatchDatabasePagesW
@ stub JetPrepareToCommitTransaction
@ stdcall JetPrepareUpdate(ptr ptr long)
@ stub JetPrereadKeys
@ stub JetReadFile
@ stub JetReadFileInstance
@ stub JetRegisterCallback
@ stub JetRemoveLogfileA
@ stub JetRemoveLogfileW
@ stub JetRenameColumn
@ stub JetRenameColumnA
@ stub JetRenameColumnW
@ stub JetRenameTable
@ stub JetRenameTableA
@ stub JetRenameTableW
@ stub JetResetCounter
@ stdcall JetResetSessionContext(ptr)
@ stub JetResetTableSequential
@ stub JetRestore2
@ stub JetRestore2A
@ stub JetRestore2W
@ stub JetRestore
@ stub JetRestoreA
@ stdcall JetRestoreInstance(ptr str str ptr)
@ stub JetRestoreInstanceA
@ stub JetRestoreInstanceW
@ stub JetRestoreW
@ stdcall JetRetrieveColumn(ptr ptr long ptr long ptr long ptr)
@ stdcall JetRetrieveColumns(ptr ptr ptr long)
@ stdcall JetRetrieveKey(ptr ptr ptr long ptr long)
@ stub JetRetrieveTaggedColumnList
@ stdcall JetRollback(ptr long)
@ stdcall JetSeek(ptr ptr long)
@ stdcall JetSetColumn(ptr ptr long ptr long long ptr)
@ stub JetSetColumnDefaultValue
@ stub JetSetColumnDefaultValueA
@ stub JetSetColumnDefaultValueW
@ stdcall JetSetColumns(ptr ptr ptr long)
@ stub JetSetCurrentIndex2
@ stub JetSetCurrentIndex2A
@ stub JetSetCurrentIndex2W
@ stub JetSetCurrentIndex3
@ stub JetSetCurrentIndex3A
@ stub JetSetCurrentIndex3W
@ stub JetSetCurrentIndex4
@ stub JetSetCurrentIndex4A
@ stub JetSetCurrentIndex4W
@ stdcall JetSetCurrentIndex(ptr ptr str)
@ stub JetSetCurrentIndexA
@ stub JetSetCurrentIndexW
@ stub JetSetDatabaseSize
@ stub JetSetDatabaseSizeA
@ stub JetSetDatabaseSizeW
@ stdcall JetSetIndexRange(ptr ptr long)
@ stub JetSetLS
@ stub JetSetMaxDatabaseSize
@ stub JetSetResourceParam
@ stdcall JetSetSessionContext(ptr ptr)
@ stdcall JetSetSystemParameter(ptr ptr long ptr str)
@ stub JetSetSystemParameterA
@ stub JetSetSystemParameterW
@ stub JetSetTableSequential
@ stub JetSnapshotStart
@ stub JetSnapshotStartA
@ stub JetSnapshotStartW
@ stub JetSnapshotStop
@ stub JetStopBackup
@ stdcall JetStopBackupInstance(ptr)
@ stub JetStopService
@ stdcall JetStopServiceInstance(ptr)
@ stdcall JetTerm2(ptr long)
@ stdcall JetTerm(ptr)
@ stub JetTest
@ stub JetTracing
@ stub JetTruncateLog
@ stub JetTruncateLogInstance
@ stub JetUnregisterCallback
@ stub JetUpdate2
@ stdcall JetUpdate(ptr ptr ptr long ptr)
@ stub JetUpgradeDatabase
@ stub JetUpgradeDatabaseA
@ stub JetUpgradeDatabaseW
