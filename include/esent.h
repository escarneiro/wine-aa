/*
 * Extensible Storage Engine (ESENT) API
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#ifndef __WINE_ESENT_H
#define __WINE_ESENT_H

#ifdef __cplusplus
extern "C" {
#endif

#define JET_API __stdcall

typedef ULONG_PTR JET_API_PTR;
typedef JET_API_PTR JET_INSTANCE;
typedef JET_API_PTR JET_SESID;
typedef JET_API_PTR JET_TABLEID;
typedef JET_API_PTR JET_HANDLE;
typedef long JET_ERR;
typedef unsigned long JET_DBID;
typedef unsigned long JET_COLUMNID;
typedef unsigned long JET_COLTYP;
typedef unsigned long JET_GRBIT;
typedef unsigned long JET_OBJTYP;
typedef unsigned long JET_CBTYP;
typedef unsigned long JET_OSSNAPID;

typedef char JET_CHAR;
typedef const char *JET_PCSTR;
typedef char *JET_PSTR;
typedef const WCHAR *JET_PCWSTR;
typedef WCHAR *JET_PWSTR;

#define JET_instanceNil   (~(JET_INSTANCE)0)
#define JET_sesidNil      (~(JET_SESID)0)
#define JET_tableidNil    (~(JET_TABLEID)0)
#define JET_dbidNil       ((JET_DBID)0xffffffff)
#define JET_bitNil        ((JET_GRBIT)0)

#define JET_cbNameMost    64
#define JET_cbColumnMost  255
#define JET_cbKeyMost     255

/* Column types */
#define JET_coltypNil            0
#define JET_coltypBit            1
#define JET_coltypUnsignedByte   2
#define JET_coltypShort          3
#define JET_coltypLong           4
#define JET_coltypCurrency       5
#define JET_coltypIEEESingle     6
#define JET_coltypIEEEDouble     7
#define JET_coltypDateTime       8
#define JET_coltypBinary         9
#define JET_coltypText           10
#define JET_coltypLongBinary     11
#define JET_coltypLongText       12
#define JET_coltypSLV            13
#define JET_coltypUnsignedLong   14
#define JET_coltypLongLong       15
#define JET_coltypGUID           16
#define JET_coltypUnsignedShort  17

/* Column bits */
#define JET_bitColumnFixed               0x00000001
#define JET_bitColumnTagged              0x00000002
#define JET_bitColumnNotNULL             0x00000004
#define JET_bitColumnVersion             0x00000008
#define JET_bitColumnAutoincrement       0x00000010
#define JET_bitColumnUpdatable           0x00000020
#define JET_bitColumnTTKey               0x00000040
#define JET_bitColumnTTDescending        0x00000080
#define JET_bitColumnMultiValued         0x00000400
#define JET_bitColumnEscrowUpdate        0x00000800
#define JET_bitColumnUnversioned         0x00001000
#define JET_bitColumnMaybeNull           0x00002000
#define JET_bitColumnFinalize            0x00004000
#define JET_bitColumnUserDefinedDefault  0x00008000
#define JET_bitColumnDeleteOnZero        0x00020000

/* Index bits */
#define JET_bitIndexUnique                0x00000001
#define JET_bitIndexPrimary               0x00000002
#define JET_bitIndexDisallowNull          0x00000004
#define JET_bitIndexIgnoreNull            0x00000008
#define JET_bitIndexIgnoreAnyNull         0x00000020
#define JET_bitIndexIgnoreFirstNull       0x00000040
#define JET_bitIndexLazyFlush             0x00000080
#define JET_bitIndexEmpty                 0x00000100
#define JET_bitIndexUnversioned           0x00000200
#define JET_bitIndexSortNullsHigh         0x00000400
#define JET_bitIndexUnicode               0x00000800
#define JET_bitIndexTuples                0x00001000
#define JET_bitIndexTupleLimits           0x00002000
#define JET_bitIndexCrossProduct          0x00004000
#define JET_bitIndexKeyMost               0x00008000
#define JET_bitIndexDisallowTruncation    0x00010000

/* JetMove() offsets and bits */
#define JET_MoveFirst     ((long)0x80000000)
#define JET_MovePrevious  (-1)
#define JET_MoveNext      (+1)
#define JET_MoveLast      ((long)0x7fffffff)
#define JET_bitMoveKeyNE  0x00000001

/* JetMakeKey() bits */
#define JET_bitNewKey                 0x00000001
#define JET_bitStrLimit               0x00000002
#define JET_bitSubStrLimit            0x00000004
#define JET_bitNormalizedKey          0x00000008
#define JET_bitKeyDataZeroLength      0x00000010
#define JET_bitFullColumnStartLimit   0x00000100
#define JET_bitFullColumnEndLimit     0x00000200
#define JET_bitPartialColumnStartLimit 0x00000400
#define JET_bitPartialColumnEndLimit  0x00000800

/* JetSeek() bits */
#define JET_bitSeekEQ             0x00000001
#define JET_bitSeekLT             0x00000002
#define JET_bitSeekLE             0x00000004
#define JET_bitSeekGE             0x00000008
#define JET_bitSeekGT             0x00000010
#define JET_bitSetIndexRange      0x00000020
#define JET_bitCheckUniqueness    0x00000040

/* JetSetIndexRange() bits */
#define JET_bitRangeInclusive        0x00000001
#define JET_bitRangeUpperLimit       0x00000002
#define JET_bitRangeInstantDuration  0x00000004
#define JET_bitRangeRemove           0x00000008

/* JetPrepareUpdate() options */
#define JET_prepInsert                    0
#define JET_prepReplace                   2
#define JET_prepCancel                    3
#define JET_prepReplaceNoLock             4
#define JET_prepInsertCopy                5
#define JET_prepInsertCopyDeleteOriginal  7

/* JetSetColumn() bits */
#define JET_bitSetAppendLV                 0x00000001
#define JET_bitSetOverwriteLV              0x00000004
#define JET_bitSetSizeLV                   0x00000008
#define JET_bitSetZeroLength               0x00000020
#define JET_bitSetSeparateLV               0x00000040
#define JET_bitSetUniqueMultiValues        0x00000080
#define JET_bitSetUniqueNormalizedMultiValues 0x00000100
#define JET_bitSetRevertToDefaultValue     0x00000200
#define JET_bitSetIntrinsicLV              0x00000400

/* JetRetrieveColumn() bits */
#define JET_bitRetrieveCopy              0x00000001
#define JET_bitRetrieveFromIndex         0x00000002
#define JET_bitRetrieveFromPrimaryBookmark 0x00000004
#define JET_bitRetrieveTag               0x00000008
#define JET_bitRetrieveNull              0x00000010
#define JET_bitRetrieveIgnoreDefault     0x00000020

/* JetEnumerateColumns() bits */
#define JET_bitEnumerateCopy                     JET_bitRetrieveCopy
#define JET_bitEnumerateIgnoreDefault            JET_bitRetrieveIgnoreDefault
#define JET_bitEnumeratePresenceOnly             0x00020000
#define JET_bitEnumerateTaggedOnly               0x00040000
#define JET_bitEnumerateCompressOutput           0x00080000
#define JET_bitEnumerateIgnoreUserDefinedDefault 0x00100000

/* Transactions */
#define JET_bitTransactionReadOnly   0x00000001
#define JET_bitCommitLazyFlush       0x00000001
#define JET_bitWaitLastLevel0Commit  0x00000002
#define JET_bitWaitAllLevel0Commit   0x00000008
#define JET_bitRollbackAll           0x00000001

/* Database bits */
#define JET_bitDbReadOnly            0x00000001
#define JET_bitDbExclusive           0x00000002
#define JET_bitDbDeleteCorruptIndexes 0x00000010
#define JET_bitDbDeleteUnicodeIndexes 0x00000400
#define JET_bitDbUpgrade             0x00000200
#define JET_bitDbOverwriteExisting   0x00000200

/* JetOpenTable() bits */
#define JET_bitTableDenyWrite        0x00000001
#define JET_bitTableDenyRead         0x00000002
#define JET_bitTableReadOnly         0x00000004
#define JET_bitTableUpdatable        0x00000008
#define JET_bitTablePermitDDL        0x00000010
#define JET_bitTableNoCache          0x00000020
#define JET_bitTablePreread          0x00000040
#define JET_bitTableSequential       0x00008000

/* JetTerm2() bits */
#define JET_bitTermComplete          0x00000001
#define JET_bitTermAbrupt            0x00000002
#define JET_bitTermStopBackup        0x00000004

/* Errors and warnings */
#define JET_errSuccess                  0
#define JET_wrnNyi                      (-1)
#define JET_wrnColumnNull               1004
#define JET_wrnBufferTruncated          1006
#define JET_wrnDatabaseAttached         1007
#define JET_wrnSeekNotEqual             1039
#define JET_wrnColumnSetNull            1068
#define JET_errFeatureNotAvailable      (-1001)
#define JET_errInvalidName              (-1002)
#define JET_errInvalidParameter         (-1003)
#define JET_errInvalidDatabaseId        (-1010)
#define JET_errOutOfMemory              (-1011)
#define JET_errNotInitialized           (-1029)
#define JET_errAlreadyInitialized       (-1030)
#define JET_errInvalidBufferSize        (-1047)
#define JET_errNotInTransaction         (-1054)
#define JET_errInvalidSesid             (-1104)
#define JET_errDatabaseDuplicate        (-1201)
#define JET_errDatabaseInUse            (-1202)
#define JET_errDatabaseNotFound         (-1203)
#define JET_errTableLocked              (-1302)
#define JET_errTableDuplicate           (-1303)
#define JET_errObjectNotFound           (-1305)
#define JET_errInvalidTableId           (-1310)
#define JET_errIndexDuplicate           (-1403)
#define JET_errIndexNotFound            (-1404)
#define JET_errColumnDuplicate          (-1508)
#define JET_errColumnNotFound           (-1507)
#define JET_errNullInvalid              (-1504)
#define JET_errRecordNotFound           (-1601)
#define JET_errNoCurrentRecord          (-1603)
#define JET_errKeyDuplicate             (-1605)
#define JET_errAlreadyPrepared          (-1607)
#define JET_errKeyNotMade               (-1608)
#define JET_errUpdateNotPrepared        (-1609)
#define JET_errFileNotFound             (-1811)

/* System parameters (a few common ones) */
#define JET_paramSystemPath             0
#define JET_paramTempPath               1
#define JET_paramLogFilePath            2
#define JET_paramBaseName               3
#define JET_paramEventSource            4
#define JET_paramMaxSessions            5
#define JET_paramMaxOpenTables          6
#define JET_paramMaxCursors             8
#define JET_paramMaxVerPages            9
#define JET_paramMaxTemporaryTables     10
#define JET_paramLogFileSize            11
#define JET_paramLogBuffers             12
#define JET_paramCircularLog            17
#define JET_paramDbExtensionSize        18
#define JET_paramPageTempDBMin          19
#define JET_paramCacheSizeMax           23
#define JET_paramCheckpointDepthMax     24
#define JET_paramRecovery               34
#define JET_paramDatabasePageSize       64
#define JET_paramMaxInstances           104
#define JET_paramCreatePathIfNotExist   100
#define JET_paramDeleteOldLogs          48
#define JET_paramNoInformationEvent     112

typedef struct
{
    unsigned long cbStruct;
    JET_COLUMNID columnid;
    JET_COLTYP coltyp;
    unsigned short wCountry;
    unsigned short langid;
    unsigned short cp;
    unsigned short wCollate;
    unsigned long cbMax;
    JET_GRBIT grbit;
} JET_COLUMNDEF;

typedef struct
{
    unsigned long cbStruct;
    char *szColumnName;
    JET_COLTYP coltyp;
    unsigned long cbMax;
    JET_GRBIT grbit;
    void *pvDefault;
    unsigned long cbDefault;
    unsigned long cp;
    JET_COLUMNID columnid;
    JET_ERR err;
} JET_COLUMNCREATE_A;

typedef struct
{
    unsigned long lcid;
    unsigned long dwMapFlags;
} JET_UNICODEINDEX;

typedef struct
{
    unsigned long chLengthMin;
    unsigned long chLengthMax;
    unsigned long chToIndexMax;
} JET_TUPLELIMITS;

typedef struct
{
    unsigned long cbStruct;
    char *szColumnName;
    JET_GRBIT grbit;
} JET_CONDITIONALCOLUMN_A;

typedef struct
{
    unsigned long cbStruct;
    char *szIndexName;
    char *szKey;
    unsigned long cbKey;
    JET_GRBIT grbit;
    unsigned long ulDensity;
    union
    {
        unsigned long lcid;
        JET_UNICODEINDEX *pidxunicode;
    } DUMMYUNIONNAME;
    union
    {
        unsigned long cbVarSegMac;
        JET_TUPLELIMITS *ptuplelimits;
    } DUMMYUNIONNAME2;
    JET_CONDITIONALCOLUMN_A *rgconditionalcolumn;
    unsigned long cConditionalColumn;
    JET_ERR err;
    unsigned long cbKeyMost;
} JET_INDEXCREATE_A;

typedef struct
{
    unsigned long cbStruct;
    char *szTableName;
    char *szTemplateTableName;
    unsigned long ulPages;
    unsigned long ulDensity;
    JET_COLUMNCREATE_A *rgcolumncreate;
    unsigned long cColumns;
    JET_INDEXCREATE_A *rgindexcreate;
    unsigned long cIndexes;
    char *szCallback;
    JET_CBTYP cbtyp;
    JET_GRBIT grbit;
    JET_TABLEID tableid;
    unsigned long cCreated;
} JET_TABLECREATE2_A;

typedef struct
{
    JET_COLUMNID columnid;
    const void *pvData;
    unsigned long cbData;
    JET_GRBIT grbit;
    unsigned long ibLongValue;
    unsigned long itagSequence;
    JET_ERR err;
} JET_SETCOLUMN;

typedef struct
{
    JET_COLUMNID columnid;
    void *pvData;
    unsigned long cbData;
    unsigned long cbActual;
    JET_GRBIT grbit;
    unsigned long ibLongValue;
    unsigned long itagSequence;
    JET_COLUMNID columnidNextTagged;
    JET_ERR err;
} JET_RETRIEVECOLUMN;

typedef struct
{
    unsigned long cbStruct;
    unsigned long centriesLT;
    unsigned long centriesInRange;
    unsigned long centriesTotal;
} JET_RECPOS;

typedef struct
{
    JET_COLUMNID columnid;
    unsigned long ctagSequence;
    unsigned long *rgtagSequence;
} JET_ENUMCOLUMNID;

typedef struct
{
    unsigned long itagSequence;
    JET_ERR err;
    unsigned long cbData;
    void *pvData;
} JET_ENUMCOLUMNVALUE;

typedef struct
{
    JET_COLUMNID columnid;
    JET_ERR err;
    union
    {
        struct
        {
            unsigned long cEnumColumnValue;
            JET_ENUMCOLUMNVALUE *rgEnumColumnValue;
        } DUMMYSTRUCTNAME;
        struct
        {
            unsigned long cbData;
            void *pvData;
        } DUMMYSTRUCTNAME2;
    } DUMMYUNIONNAME;
} JET_ENUMCOLUMN;

typedef struct
{
    unsigned long cbStruct;
    unsigned long ibLongValue;
    unsigned long itagSequence;
    JET_COLUMNID columnidNextTagged;
} JET_RETINFO;

typedef struct
{
    unsigned long cbStruct;
    unsigned long ibLongValue;
    unsigned long itagSequence;
} JET_SETINFO;

typedef struct
{
    unsigned long cbStruct;
    JET_COLUMNID columnid;
    JET_COLTYP coltyp;
    unsigned short wCountry;
    unsigned short langid;
    unsigned short cp;
    unsigned short wFiller;
    unsigned long cbMax;
    JET_GRBIT grbit;
    char szBaseTableName[256];
    char szBaseColumnName[256];
} JET_COLUMNBASE_A;

/* JetGetTableColumnInfo() / JetGetColumnInfo() levels */
#define JET_ColInfo                 0
#define JET_ColInfoList             1
#define JET_ColInfoSysTabCursor     3
#define JET_ColInfoBase             4
#define JET_ColInfoListCompact      5
#define JET_ColInfoByColid          6
#define JET_ColInfoListSortColumnid 7

/* JetGetTableIndexInfo() levels */
#define JET_IdxInfo                 0
#define JET_IdxInfoList             1
#define JET_IdxInfoSysTabCursor     2
#define JET_IdxInfoOLC              3
#define JET_IdxInfoResetOLC         4
#define JET_IdxInfoSpaceAlloc       5
#define JET_IdxInfoLangid           6
#define JET_IdxInfoCount            7
#define JET_IdxInfoVarSegMac        8
#define JET_IdxInfoIndexId          9

/* JetGetDatabaseInfo() levels */
#define JET_DbInfoFilename          0
#define JET_DbInfoConnect           1
#define JET_DbInfoCountry           2
#define JET_DbInfoLCID              3
#define JET_DbInfoCp                5
#define JET_DbInfoCollate           6
#define JET_DbInfoOptions           7
#define JET_DbInfoTransactions      8
#define JET_DbInfoVersion           9
#define JET_DbInfoFilesize          10
#define JET_DbInfoSpaceOwned        11
#define JET_DbInfoSpaceAvailable    12
#define JET_DbInfoUpgrade           13
#define JET_DbInfoMisc              14
#define JET_DbInfoDBInUse           15
#define JET_DbInfoPageSize          17

typedef void * (JET_API *JET_PFNREALLOC)(void *pvContext, void *pv, unsigned long cb);

#ifdef __cplusplus
}
#endif

#endif /* __WINE_ESENT_H */
