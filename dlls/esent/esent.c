/*
 * Extensible Storage Engine (ESENT)
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

/*
 * This is a small in-memory implementation of the ESENT API, sized for
 * applications that use a database as a local cache. Databases are not
 * written to disk yet, so they start empty in each process.
 */

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "windef.h"
#include "winbase.h"
#include "winnls.h"
#include "esent.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(esent);

#define MAX_SEGMENTS 12

struct value
{
    BYTE *data;
    ULONG len;
};

/* All values of one column in one record; empty means NULL. */
struct values
{
    struct value *items;
    ULONG count;
};

struct column
{
    char name[JET_cbNameMost + 1];
    JET_COLUMNID id;
    JET_COLTYP type;
    ULONG cb_max;
    JET_GRBIT grbit;
    ULONG cp;
    struct value def;
};

struct segment
{
    unsigned int column;  /* index into table->columns */
    BOOL descending;
};

struct index
{
    char name[JET_cbNameMost + 1];
    JET_GRBIT grbit;
    unsigned int segment_count;
    struct segment segments[MAX_SEGMENTS];
};

struct record
{
    LONGLONG id;
    struct values *columns;  /* one per table column */
};

struct table
{
    char name[JET_cbNameMost + 1];
    struct column *columns;
    unsigned int column_count;
    struct index *indexes;
    unsigned int index_count;
    struct record **records;
    unsigned int record_count, record_size;
    LONGLONG next_record_id;
    ULONG next_autoinc;
    JET_COLUMNID next_fixed, next_variable, next_tagged;
    ULONG version;  /* bumped on every change */
};

struct database
{
    char path[MAX_PATH];
    struct table **tables;
    unsigned int table_count;
    struct database *next;
};

struct instance
{
    char name[64];
    BOOL initialized;
    struct database *databases;  /* attached databases */
};

struct undo;

struct session
{
    struct instance *instance;
    unsigned int transaction_level;
    struct undo *undo;  /* changes made in open transactions, newest first */
    struct database *open_dbs[64];  /* indexed by dbid - 1 */
};

/* A key made with JetMakeKey(), or the key of an index entry. */
struct key
{
    unsigned int count;
    struct value segments[MAX_SEGMENTS];
    BOOL null[MAX_SEGMENTS];
    int limit;  /* 0, or -1/+1: compare as smaller/greater than equal keys */
    BOOL prefix;  /* the last segment matches by prefix */
};

/* An index entry: a record and, for a multi-valued first segment, which value. */
struct entry
{
    struct record *record;
    ULONG tag;
};

struct cursor
{
    struct session *session;
    struct database *db;
    struct table *table;
    int index;  /* -1: no index, records in insertion order */

    struct entry *entries;
    unsigned int entry_count;
    ULONG entries_version;

    /* position: on entries[pos] if on_record, else before entries[pos] */
    unsigned int pos;
    BOOL on_record;
    struct record *current;
    ULONG current_tag;

    struct key key;
    BOOL key_made;

    BOOL range;
    struct key range_key;
    JET_GRBIT range_grbit;

    ULONG prep;
    BOOL prepared;
    struct record *copy;  /* the copy buffer of a prepared update */
};

enum undo_type
{
    UNDO_INSERT,   /* a record was inserted: delete it */
    UNDO_REPLACE,  /* a record was replaced: put the old one back */
    UNDO_DELETE,   /* a record was deleted: insert it again */
};

struct undo
{
    struct undo *next;
    unsigned int level;
    enum undo_type type;
    struct table *table;
    LONGLONG id;
    struct record *old;
    unsigned int old_column_count;
};

static struct instance global_instance;
static ULONG_PTR global_params[256];
static char *global_param_strings[256];

static CRITICAL_SECTION esent_cs;
static CRITICAL_SECTION_DEBUG esent_cs_debug =
{
    0, 0, &esent_cs,
    { &esent_cs_debug.ProcessLocksList, &esent_cs_debug.ProcessLocksList },
      0, 0, { (DWORD_PTR)(__FILE__ ": esent_cs") }
};
static CRITICAL_SECTION esent_cs = { &esent_cs_debug, -1, 0, 0, 0, 0 };

static struct database *all_databases;

static char *strdup_a(const char *s)
{
    char *ret;
    if (!s) return NULL;
    if ((ret = malloc(strlen(s) + 1))) strcpy(ret, s);
    return ret;
}

static void copy_name(char *dst, const char *src)
{
    lstrcpynA(dst, src ? src : "", JET_cbNameMost + 1);
}

static BOOL value_set(struct value *v, const void *data, ULONG len)
{
    BYTE *p = NULL;
    if (len && !(p = malloc(len))) return FALSE;
    if (len) memcpy(p, data, len);
    free(v->data);
    v->data = p;
    v->len = len;
    return TRUE;
}

static void values_clear(struct values *vals)
{
    ULONG i;
    for (i = 0; i < vals->count; ++i) free(vals->items[i].data);
    free(vals->items);
    vals->items = NULL;
    vals->count = 0;
}

static BOOL values_copy(struct values *dst, const struct values *src)
{
    ULONG i;
    dst->items = NULL;
    dst->count = 0;
    if (!src->count) return TRUE;
    if (!(dst->items = calloc(src->count, sizeof(*dst->items)))) return FALSE;
    for (i = 0; i < src->count; ++i)
    {
        if (!value_set(&dst->items[i], src->items[i].data, src->items[i].len)) return FALSE;
        ++dst->count;
    }
    return TRUE;
}

static void record_free(struct record *rec, unsigned int column_count)
{
    unsigned int i;
    if (!rec) return;
    for (i = 0; i < column_count; ++i) values_clear(&rec->columns[i]);
    free(rec->columns);
    free(rec);
}

static struct record *record_create(const struct table *table)
{
    struct record *rec;
    if (!(rec = calloc(1, sizeof(*rec)))) return NULL;
    if (table->column_count && !(rec->columns = calloc(table->column_count, sizeof(*rec->columns))))
    {
        free(rec);
        return NULL;
    }
    return rec;
}

static struct record *record_copy(const struct table *table, const struct record *src)
{
    struct record *rec;
    unsigned int i;
    if (!(rec = record_create(table))) return NULL;
    rec->id = src->id;
    for (i = 0; i < table->column_count; ++i)
    {
        if (!values_copy(&rec->columns[i], &src->columns[i]))
        {
            record_free(rec, table->column_count);
            return NULL;
        }
    }
    return rec;
}

static void table_free(struct table *table)
{
    unsigned int i;
    if (!table) return;
    for (i = 0; i < table->record_count; ++i) record_free(table->records[i], table->column_count);
    for (i = 0; i < table->column_count; ++i) free(table->columns[i].def.data);
    free(table->records);
    free(table->columns);
    free(table->indexes);
    free(table);
}

static struct table *find_table(struct database *db, const char *name)
{
    unsigned int i;
    for (i = 0; i < db->table_count; ++i)
        if (!lstrcmpiA(db->tables[i]->name, name)) return db->tables[i];
    return NULL;
}

static int find_column(const struct table *table, const char *name)
{
    unsigned int i;
    for (i = 0; i < table->column_count; ++i)
        if (!lstrcmpiA(table->columns[i].name, name)) return i;
    return -1;
}

static int find_column_id(const struct table *table, JET_COLUMNID id)
{
    unsigned int i;
    for (i = 0; i < table->column_count; ++i)
        if (table->columns[i].id == id) return i;
    return -1;
}

static int find_index(const struct table *table, const char *name)
{
    unsigned int i;
    for (i = 0; i < table->index_count; ++i)
        if (!lstrcmpiA(table->indexes[i].name, name)) return i;
    return -1;
}

static int primary_index(const struct table *table)
{
    unsigned int i;
    for (i = 0; i < table->index_count; ++i)
        if (table->indexes[i].grbit & JET_bitIndexPrimary) return i;
    return -1;
}

static void table_changed(struct table *table)
{
    ++table->version;
}

/* Value comparison for keys. */

static BOOL is_text(const struct column *col)
{
    return col->type == JET_coltypText || col->type == JET_coltypLongText;
}

static int compare_values(const struct column *col, const struct value *a, const struct value *b, BOOL prefix)
{
    ULONG len;
    int ret;

    switch (col->type)
    {
    case JET_coltypBit:
    case JET_coltypUnsignedByte:
        return (a->len ? a->data[0] : 0) - (b->len ? b->data[0] : 0);
    case JET_coltypShort:
    {
        short x = 0, y = 0;
        memcpy(&x, a->data, min(a->len, sizeof(x)));
        memcpy(&y, b->data, min(b->len, sizeof(y)));
        return (x > y) - (x < y);
    }
    case JET_coltypUnsignedShort:
    {
        unsigned short x = 0, y = 0;
        memcpy(&x, a->data, min(a->len, sizeof(x)));
        memcpy(&y, b->data, min(b->len, sizeof(y)));
        return (x > y) - (x < y);
    }
    case JET_coltypLong:
    {
        LONG x = 0, y = 0;
        memcpy(&x, a->data, min(a->len, sizeof(x)));
        memcpy(&y, b->data, min(b->len, sizeof(y)));
        return (x > y) - (x < y);
    }
    case JET_coltypUnsignedLong:
    {
        ULONG x = 0, y = 0;
        memcpy(&x, a->data, min(a->len, sizeof(x)));
        memcpy(&y, b->data, min(b->len, sizeof(y)));
        return (x > y) - (x < y);
    }
    case JET_coltypLongLong:
    case JET_coltypCurrency:
    {
        LONGLONG x = 0, y = 0;
        memcpy(&x, a->data, min(a->len, sizeof(x)));
        memcpy(&y, b->data, min(b->len, sizeof(y)));
        return (x > y) - (x < y);
    }
    case JET_coltypIEEESingle:
    {
        float x = 0, y = 0;
        memcpy(&x, a->data, min(a->len, sizeof(x)));
        memcpy(&y, b->data, min(b->len, sizeof(y)));
        return (x > y) - (x < y);
    }
    case JET_coltypIEEEDouble:
    case JET_coltypDateTime:
    {
        double x = 0, y = 0;
        memcpy(&x, a->data, min(a->len, sizeof(x)));
        memcpy(&y, b->data, min(b->len, sizeof(y)));
        return (x > y) - (x < y);
    }
    }

    if (is_text(col))
    {
        if (col->cp == 1200)
        {
            int la = a->len / sizeof(WCHAR), lb = b->len / sizeof(WCHAR);
            /* trailing NULs are not significant */
            while (la && !((WCHAR *)a->data)[la - 1]) --la;
            while (lb && !((WCHAR *)b->data)[lb - 1]) --lb;
            if (prefix && la > lb) la = lb;
            ret = CompareStringW(LOCALE_INVARIANT, NORM_IGNORECASE | NORM_IGNOREWIDTH | NORM_IGNOREKANATYPE,
                    (WCHAR *)a->data, la, (WCHAR *)b->data, lb);
        }
        else
        {
            int la = a->len, lb = b->len;
            while (la && !a->data[la - 1]) --la;
            while (lb && !b->data[lb - 1]) --lb;
            if (prefix && la > lb) la = lb;
            ret = CompareStringA(LOCALE_INVARIANT, NORM_IGNORECASE | NORM_IGNOREWIDTH | NORM_IGNOREKANATYPE,
                    (char *)a->data, la, (char *)b->data, lb);
        }
        return ret ? ret - CSTR_EQUAL : 0;
    }

    /* binary, GUID */
    len = min(a->len, b->len);
    if ((ret = len ? memcmp(a->data, b->data, len) : 0)) return ret;
    if (prefix && a->len >= b->len) return 0;
    return (a->len > b->len) - (a->len < b->len);
}

/* Builds the key of an index entry. */
static void entry_key(const struct table *table, const struct index *index, const struct entry *entry, struct key *key)
{
    unsigned int i;

    memset(key, 0, sizeof(*key));
    key->count = index->segment_count;
    for (i = 0; i < index->segment_count; ++i)
    {
        const struct values *vals = &entry->record->columns[index->segments[i].column];
        ULONG tag = (i == 0) ? entry->tag : 1;

        if (tag && tag <= vals->count)
            key->segments[i] = vals->items[tag - 1];
        else
            key->null[i] = TRUE;
    }
}

/* Compares an entry key (full) with a search key (possibly partial). */
static int compare_keys(const struct table *table, const struct index *index, const struct key *a, const struct key *b)
{
    unsigned int i, count = min(a->count, b->count);
    int ret;

    for (i = 0; i < count; ++i)
    {
        const struct column *col = &table->columns[index->segments[i].column];
        BOOL prefix = b->prefix && i == b->count - 1;

        if (a->null[i] || b->null[i])
        {
            BOOL high = !!(index->grbit & JET_bitIndexSortNullsHigh);
            if (a->null[i] && b->null[i]) continue;
            ret = a->null[i] ? -1 : 1;
            if (high) ret = -ret;
        }
        else
        {
            ret = compare_values(col, &a->segments[i], &b->segments[i], prefix);
        }
        if (index->segments[i].descending) ret = -ret;
        if (ret) return ret > 0 ? 1 : -1;
    }
    if (b->limit) return -b->limit;
    return (a->count > b->count) - (a->count < b->count);
}

static BOOL entry_included(const struct table *table, const struct index *index, const struct entry *entry)
{
    struct key key;
    unsigned int i, nulls = 0;

    entry_key(table, index, entry, &key);
    for (i = 0; i < key.count; ++i) nulls += key.null[i];

    if ((index->grbit & JET_bitIndexIgnoreAnyNull) && nulls) return FALSE;
    if ((index->grbit & JET_bitIndexIgnoreFirstNull) && key.count && key.null[0]) return FALSE;
    if ((index->grbit & JET_bitIndexIgnoreNull) && nulls == key.count) return FALSE;
    return TRUE;
}

static const struct table *sort_table;
static const struct index *sort_index;

static int __cdecl compare_entries(const void *pa, const void *pb)
{
    const struct entry *a = pa, *b = pb;
    struct key ka, kb;
    int ret;

    entry_key(sort_table, sort_index, a, &ka);
    entry_key(sort_table, sort_index, b, &kb);
    if ((ret = compare_keys(sort_table, sort_index, &ka, &kb))) return ret;
    if (a->record->id != b->record->id) return a->record->id < b->record->id ? -1 : 1;
    return (a->tag > b->tag) - (a->tag < b->tag);
}

/* Rebuilds the cursor's sorted entries after the table changed. */
static BOOL cursor_update_entries(struct cursor *cursor)
{
    struct table *table = cursor->table;
    unsigned int i, count = 0;
    struct entry *entries;
    ULONG t;

    if (cursor->entries && cursor->entries_version == table->version) return TRUE;

    for (i = 0; i < table->record_count; ++i)
    {
        if (cursor->index >= 0)
        {
            const struct values *first = &table->records[i]->columns[table->indexes[cursor->index].segments[0].column];
            count += first->count > 1 ? first->count : 1;
        }
        else ++count;
    }

    if (!(entries = calloc(max(count, 1), sizeof(*entries)))) return FALSE;
    count = 0;
    for (i = 0; i < table->record_count; ++i)
    {
        struct record *rec = table->records[i];

        if (cursor->index < 0)
        {
            entries[count].record = rec;
            entries[count++].tag = 1;
            continue;
        }
        else
        {
            const struct index *index = &table->indexes[cursor->index];
            const struct values *first = &rec->columns[index->segments[0].column];
            ULONG tags = first->count > 1 ? first->count : 1;

            for (t = 1; t <= tags; ++t)
            {
                entries[count].record = rec;
                entries[count].tag = first->count ? t : 0;
                if (entry_included(table, index, &entries[count])) ++count;
            }
        }
    }

    if (cursor->index >= 0)
    {
        sort_table = table;
        sort_index = &table->indexes[cursor->index];
        qsort(entries, count, sizeof(*entries), compare_entries);
    }

    free(cursor->entries);
    cursor->entries = entries;
    cursor->entry_count = count;
    cursor->entries_version = table->version;

    /* find the current record again */
    if (cursor->on_record)
    {
        for (i = 0; i < count; ++i)
            if (entries[i].record == cursor->current && entries[i].tag == cursor->current_tag) break;
        if (i < count)
            cursor->pos = i;
        else
        {
            cursor->on_record = FALSE;
            cursor->current = NULL;
            cursor->pos = min(cursor->pos, count);
        }
    }
    else cursor->pos = min(cursor->pos, count);
    return TRUE;
}

static BOOL entry_in_range(struct cursor *cursor, unsigned int pos)
{
    struct key key;
    int cmp;

    if (!cursor->range || cursor->index < 0) return TRUE;
    entry_key(cursor->table, &cursor->table->indexes[cursor->index], &cursor->entries[pos], &key);
    cmp = compare_keys(cursor->table, &cursor->table->indexes[cursor->index], &key, &cursor->range_key);
    if (cursor->range_grbit & JET_bitRangeUpperLimit)
        return (cursor->range_grbit & JET_bitRangeInclusive) ? cmp <= 0 : cmp < 0;
    return (cursor->range_grbit & JET_bitRangeInclusive) ? cmp >= 0 : cmp > 0;
}

static void cursor_set_position(struct cursor *cursor, unsigned int pos)
{
    cursor->pos = pos;
    cursor->on_record = TRUE;
    cursor->current = cursor->entries[pos].record;
    cursor->current_tag = cursor->entries[pos].tag;
}

static struct record *cursor_record(struct cursor *cursor)
{
    if (!cursor_update_entries(cursor) || !cursor->on_record) return NULL;
    return cursor->current;
}

static void free_cursor(struct cursor *cursor)
{
    if (cursor->copy) record_free(cursor->copy, cursor->table->column_count);
    free(cursor->entries);
    free(cursor);
}

static struct session *get_session(JET_SESID sesid)
{
    return (sesid && sesid != JET_sesidNil) ? (struct session *)sesid : NULL;
}

static struct cursor *get_cursor(JET_TABLEID tableid)
{
    return (tableid && tableid != JET_tableidNil) ? (struct cursor *)tableid : NULL;
}

static struct database *get_database(struct session *session, JET_DBID dbid)
{
    if (!session || !dbid || dbid > ARRAY_SIZE(session->open_dbs)) return NULL;
    return session->open_dbs[dbid - 1];
}

static struct instance *get_instance(JET_INSTANCE instance)
{
    if (!instance || instance == JET_instanceNil) return &global_instance;
    return (struct instance *)instance;
}

/* Transactions: each session logs the changes it makes, so that it can undo them. */

static int find_record(const struct table *table, LONGLONG id)
{
    unsigned int i;
    for (i = 0; i < table->record_count; ++i)
        if (table->records[i]->id == id) return i;
    return -1;
}

/* Makes a record saved before columns were added match the table again. */
static BOOL record_fix_columns(const struct table *table, struct record *rec, unsigned int column_count)
{
    struct values *vals;

    if (column_count >= table->column_count) return TRUE;
    if (!(vals = realloc(rec->columns, table->column_count * sizeof(*vals)))) return FALSE;
    memset(&vals[column_count], 0, (table->column_count - column_count) * sizeof(*vals));
    rec->columns = vals;
    return TRUE;
}

/* Inserts a record, keeping the records in insertion (id) order. */
static BOOL table_insert_record(struct table *table, struct record *rec)
{
    unsigned int i;

    if (table->record_count == table->record_size)
    {
        unsigned int new_size = max(16, table->record_size * 2);
        struct record **records = realloc(table->records, new_size * sizeof(*records));
        if (!records) return FALSE;
        table->records = records;
        table->record_size = new_size;
    }
    for (i = table->record_count; i > 0 && table->records[i - 1]->id > rec->id; --i)
        table->records[i] = table->records[i - 1];
    table->records[i] = rec;
    ++table->record_count;
    return TRUE;
}

static void table_remove_record(struct table *table, unsigned int i)
{
    memmove(&table->records[i], &table->records[i + 1], (table->record_count - i - 1) * sizeof(*table->records));
    --table->record_count;
}

/* Logs a change; takes ownership of the old record. */
static void log_undo(struct session *session, enum undo_type type, struct table *table, LONGLONG id,
        struct record *old)
{
    struct undo *undo;

    if (session && session->transaction_level && (undo = calloc(1, sizeof(*undo))))
    {
        undo->level = session->transaction_level;
        undo->type = type;
        undo->table = table;
        undo->id = id;
        undo->old = old;
        undo->old_column_count = table->column_count;
        undo->next = session->undo;
        session->undo = undo;
        return;
    }
    if (session && session->transaction_level) ERR("Failed to log change, it can't be rolled back.\n");
    if (old) record_free(old, table->column_count);
}

static void free_undo(struct undo *undo)
{
    if (undo->old) record_free(undo->old, undo->old_column_count);
    free(undo);
}

/* Undoes the changes of the innermost transaction level. */
static void rollback_level(struct session *session)
{
    struct undo *undo;

    while ((undo = session->undo) && undo->level == session->transaction_level)
    {
        struct table *table = undo->table;
        int i;

        session->undo = undo->next;
        switch (undo->type)
        {
        case UNDO_INSERT:
            if ((i = find_record(table, undo->id)) >= 0)
            {
                record_free(table->records[i], table->column_count);
                table_remove_record(table, i);
            }
            break;

        case UNDO_REPLACE:
            if ((i = find_record(table, undo->old->id)) >= 0
                    && record_fix_columns(table, undo->old, undo->old_column_count))
            {
                record_free(table->records[i], table->column_count);
                table->records[i] = undo->old;
                undo->old = NULL;
            }
            break;

        case UNDO_DELETE:
            if (record_fix_columns(table, undo->old, undo->old_column_count)
                    && table_insert_record(table, undo->old))
                undo->old = NULL;
            break;
        }
        table_changed(table);
        free_undo(undo);
    }
    --session->transaction_level;
}

/* Hands the changes of the innermost level to the outer one, or forgets them. */
static void commit_level(struct session *session)
{
    struct undo *undo, **next = &session->undo;

    while ((undo = *next) && undo->level == session->transaction_level)
    {
        if (session->transaction_level == 1)
        {
            *next = undo->next;
            free_undo(undo);
        }
        else
        {
            --undo->level;
            next = &undo->next;
        }
    }
    --session->transaction_level;
}

/***********************************************************************
 * System parameters, instances and sessions
 */

JET_ERR JET_API JetSetSystemParameter(JET_INSTANCE *instance, JET_SESID sesid, unsigned long paramid,
        JET_API_PTR param, JET_PCSTR str)
{
    TRACE("%p, %#Ix, %lu, %#Ix, %s.\n", instance, sesid, paramid, param, debugstr_a(str));

    if (paramid < ARRAY_SIZE(global_params))
    {
        EnterCriticalSection(&esent_cs);
        global_params[paramid] = param;
        free(global_param_strings[paramid]);
        global_param_strings[paramid] = strdup_a(str);
        LeaveCriticalSection(&esent_cs);
    }
    return JET_errSuccess;
}

JET_ERR JET_API JetGetSystemParameter(JET_INSTANCE instance, JET_SESID sesid, unsigned long paramid,
        JET_API_PTR *param, JET_PSTR str, unsigned long size)
{
    TRACE("%#Ix, %#Ix, %lu, %p, %p, %lu.\n", instance, sesid, paramid, param, str, size);

    if (paramid >= ARRAY_SIZE(global_params)) return JET_errInvalidParameter;

    EnterCriticalSection(&esent_cs);
    if (param) *param = global_params[paramid];
    if (str && size) lstrcpynA(str, global_param_strings[paramid] ? global_param_strings[paramid] : "", size);
    LeaveCriticalSection(&esent_cs);
    return JET_errSuccess;
}

JET_ERR JET_API JetCreateInstance(JET_INSTANCE *instance, const char *name)
{
    struct instance *inst;

    TRACE("%p, %s.\n", instance, debugstr_a(name));

    if (!instance) return JET_errInvalidParameter;
    if (!(inst = calloc(1, sizeof(*inst)))) return JET_errOutOfMemory;
    lstrcpynA(inst->name, name ? name : "", sizeof(inst->name));
    *instance = (JET_INSTANCE)inst;
    return JET_errSuccess;
}

JET_ERR JET_API JetInit(JET_INSTANCE *instance)
{
    struct instance *inst;

    TRACE("%p.\n", instance);

    inst = get_instance(instance ? *instance : 0);
    if (inst->initialized) return JET_errAlreadyInitialized;
    inst->initialized = TRUE;
    if (instance && (!*instance || *instance == JET_instanceNil)) *instance = (JET_INSTANCE)inst;
    return JET_errSuccess;
}

JET_ERR JET_API JetTerm2(JET_INSTANCE instance, JET_GRBIT grbit)
{
    struct instance *inst = get_instance(instance);

    TRACE("%#Ix, %#lx.\n", instance, grbit);

    inst->initialized = FALSE;
    if (inst != &global_instance) free(inst);
    return JET_errSuccess;
}

JET_ERR JET_API JetTerm(JET_INSTANCE instance)
{
    return JetTerm2(instance, 0);
}

JET_ERR JET_API JetStopServiceInstance(JET_INSTANCE instance)
{
    TRACE("%#Ix.\n", instance);
    return JET_errSuccess;
}

JET_ERR JET_API JetBeginSession(JET_INSTANCE instance, JET_SESID *sesid, JET_PCSTR user, JET_PCSTR password)
{
    struct session *session;

    TRACE("%#Ix, %p, %s, %s.\n", instance, sesid, debugstr_a(user), debugstr_a(password));

    if (!sesid) return JET_errInvalidParameter;
    if (!(session = calloc(1, sizeof(*session)))) return JET_errOutOfMemory;
    session->instance = get_instance(instance);
    *sesid = (JET_SESID)session;
    return JET_errSuccess;
}

JET_ERR JET_API JetEndSession(JET_SESID sesid, JET_GRBIT grbit)
{
    struct session *session = get_session(sesid);

    TRACE("%#Ix, %#lx.\n", sesid, grbit);

    if (!session) return JET_errInvalidSesid;
    EnterCriticalSection(&esent_cs);
    while (session->transaction_level) rollback_level(session);
    LeaveCriticalSection(&esent_cs);
    free(session);
    return JET_errSuccess;
}

JET_ERR JET_API JetSetSessionContext(JET_SESID sesid, JET_API_PTR context)
{
    TRACE("%#Ix, %#Ix.\n", sesid, context);
    return JET_errSuccess;
}

JET_ERR JET_API JetResetSessionContext(JET_SESID sesid)
{
    TRACE("%#Ix.\n", sesid);
    return JET_errSuccess;
}

JET_ERR JET_API JetIdle(JET_SESID sesid, JET_GRBIT grbit)
{
    TRACE("%#Ix, %#lx.\n", sesid, grbit);
    return JET_errSuccess;
}

/***********************************************************************
 * Transactions
 */

JET_ERR JET_API JetBeginTransaction2(JET_SESID sesid, JET_GRBIT grbit)
{
    struct session *session = get_session(sesid);

    TRACE("%#Ix, %#lx.\n", sesid, grbit);

    if (!session) return JET_errInvalidSesid;
    EnterCriticalSection(&esent_cs);
    ++session->transaction_level;
    LeaveCriticalSection(&esent_cs);
    return JET_errSuccess;
}

JET_ERR JET_API JetBeginTransaction(JET_SESID sesid)
{
    return JetBeginTransaction2(sesid, 0);
}

JET_ERR JET_API JetCommitTransaction(JET_SESID sesid, JET_GRBIT grbit)
{
    struct session *session = get_session(sesid);

    TRACE("%#Ix, %#lx.\n", sesid, grbit);

    if (!session) return JET_errInvalidSesid;
    if (!session->transaction_level) return JET_errNotInTransaction;
    EnterCriticalSection(&esent_cs);
    commit_level(session);
    LeaveCriticalSection(&esent_cs);
    return JET_errSuccess;
}

JET_ERR JET_API JetRollback(JET_SESID sesid, JET_GRBIT grbit)
{
    struct session *session = get_session(sesid);

    TRACE("%#Ix, %#lx.\n", sesid, grbit);

    if (!session) return JET_errInvalidSesid;
    if (!session->transaction_level) return JET_errNotInTransaction;
    EnterCriticalSection(&esent_cs);
    do
    {
        rollback_level(session);
    } while ((grbit & JET_bitRollbackAll) && session->transaction_level);
    LeaveCriticalSection(&esent_cs);
    return JET_errSuccess;
}

/***********************************************************************
 * Databases
 */

static struct database *find_database(const char *path)
{
    struct database *db;
    for (db = all_databases; db; db = db->next)
        if (!lstrcmpiA(db->path, path)) return db;
    return NULL;
}

static JET_ERR open_database(struct session *session, struct database *db, JET_DBID *dbid)
{
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(session->open_dbs); ++i)
    {
        if (!session->open_dbs[i])
        {
            session->open_dbs[i] = db;
            if (dbid) *dbid = i + 1;
            return JET_errSuccess;
        }
    }
    return JET_errOutOfMemory;
}

JET_ERR JET_API JetCreateDatabase(JET_SESID sesid, JET_PCSTR path, JET_PCSTR connect, JET_DBID *dbid, JET_GRBIT grbit)
{
    struct session *session = get_session(sesid);
    struct database *db;
    JET_ERR ret;

    TRACE("%#Ix, %s, %s, %p, %#lx.\n", sesid, debugstr_a(path), debugstr_a(connect), dbid, grbit);

    if (!session) return JET_errInvalidSesid;
    if (!path) return JET_errInvalidParameter;

    EnterCriticalSection(&esent_cs);
    if ((db = find_database(path)))
    {
        if (!(grbit & JET_bitDbOverwriteExisting))
        {
            LeaveCriticalSection(&esent_cs);
            return JET_errDatabaseDuplicate;
        }
    }
    else
    {
        if (!(db = calloc(1, sizeof(*db))))
        {
            LeaveCriticalSection(&esent_cs);
            return JET_errOutOfMemory;
        }
        lstrcpynA(db->path, path, sizeof(db->path));
        db->next = all_databases;
        all_databases = db;
    }
    ret = open_database(session, db, dbid);
    LeaveCriticalSection(&esent_cs);
    return ret;
}

JET_ERR JET_API JetAttachDatabase(JET_SESID sesid, JET_PCSTR path, JET_GRBIT grbit)
{
    JET_ERR ret;

    TRACE("%#Ix, %s, %#lx.\n", sesid, debugstr_a(path), grbit);

    if (!get_session(sesid)) return JET_errInvalidSesid;
    EnterCriticalSection(&esent_cs);
    ret = find_database(path) ? JET_wrnDatabaseAttached : JET_errFileNotFound;
    LeaveCriticalSection(&esent_cs);
    TRACE("returning %ld.\n", ret);
    return ret;
}

JET_ERR JET_API JetDetachDatabase(JET_SESID sesid, JET_PCSTR path)
{
    TRACE("%#Ix, %s.\n", sesid, debugstr_a(path));
    return JET_errSuccess;
}

JET_ERR JET_API JetOpenDatabase(JET_SESID sesid, JET_PCSTR path, JET_PCSTR connect, JET_DBID *dbid, JET_GRBIT grbit)
{
    struct session *session = get_session(sesid);
    struct database *db;
    JET_ERR ret;

    TRACE("%#Ix, %s, %s, %p, %#lx.\n", sesid, debugstr_a(path), debugstr_a(connect), dbid, grbit);

    if (!session) return JET_errInvalidSesid;
    EnterCriticalSection(&esent_cs);
    if ((db = find_database(path)))
        ret = open_database(session, db, dbid);
    else
        ret = JET_errDatabaseNotFound;
    LeaveCriticalSection(&esent_cs);
    TRACE("returning %ld.\n", ret);
    return ret;
}

JET_ERR JET_API JetCloseDatabase(JET_SESID sesid, JET_DBID dbid, JET_GRBIT grbit)
{
    struct session *session = get_session(sesid);

    TRACE("%#Ix, %lu, %#lx.\n", sesid, dbid, grbit);

    if (!get_database(session, dbid)) return JET_errInvalidDatabaseId;
    session->open_dbs[dbid - 1] = NULL;
    return JET_errSuccess;
}

JET_ERR JET_API JetGetDatabaseInfo(JET_SESID sesid, JET_DBID dbid, void *result, unsigned long size, unsigned long level)
{
    struct database *db = get_database(get_session(sesid), dbid);

    TRACE("%#Ix, %lu, %p, %lu, %lu.\n", sesid, dbid, result, size, level);

    if (!db) return JET_errInvalidDatabaseId;

    switch (level)
    {
    case JET_DbInfoFilename:
        if (size < strlen(db->path) + 1) return JET_errInvalidBufferSize;
        strcpy(result, db->path);
        return JET_errSuccess;
    case JET_DbInfoFilesize:
    case JET_DbInfoSpaceOwned:
        if (size < sizeof(ULONG)) return JET_errInvalidBufferSize;
        *(ULONG *)result = 256;
        return JET_errSuccess;
    case JET_DbInfoSpaceAvailable:
        if (size < sizeof(ULONG)) return JET_errInvalidBufferSize;
        *(ULONG *)result = 0;
        return JET_errSuccess;
    case JET_DbInfoPageSize:
        if (size < sizeof(ULONG)) return JET_errInvalidBufferSize;
        *(ULONG *)result = 4096;
        return JET_errSuccess;
    }
    FIXME("Unhandled level %lu.\n", level);
    return JET_errFeatureNotAvailable;
}

JET_ERR JET_API JetGrowDatabase(JET_SESID sesid, JET_DBID dbid, unsigned long pages, unsigned long *real)
{
    TRACE("%#Ix, %lu, %lu, %p.\n", sesid, dbid, pages, real);
    if (real) *real = pages;
    return JET_errSuccess;
}

JET_ERR JET_API JetDefragment(JET_SESID sesid, JET_DBID dbid, JET_PCSTR table, unsigned long *passes,
        unsigned long *seconds, JET_GRBIT grbit)
{
    TRACE("%#Ix, %lu, %s, %p, %p, %#lx.\n", sesid, dbid, debugstr_a(table), passes, seconds, grbit);
    if (passes) *passes = 0;
    if (seconds) *seconds = 0;
    return JET_errSuccess;
}

JET_ERR JET_API JetDefragment2(JET_SESID sesid, JET_DBID dbid, JET_PCSTR table, unsigned long *passes,
        unsigned long *seconds, void *callback, JET_GRBIT grbit)
{
    return JetDefragment(sesid, dbid, table, passes, seconds, grbit);
}

JET_ERR JET_API JetBackupInstance(JET_INSTANCE instance, JET_PCSTR path, JET_GRBIT grbit, void *status)
{
    FIXME("%#Ix, %s, %#lx, %p, stub!\n", instance, debugstr_a(path), grbit, status);
    return JET_errFeatureNotAvailable;
}

JET_ERR JET_API JetStopBackupInstance(JET_INSTANCE instance)
{
    TRACE("%#Ix.\n", instance);
    return JET_errSuccess;
}

JET_ERR JET_API JetRestoreInstance(JET_INSTANCE instance, JET_PCSTR source, JET_PCSTR dest, void *status)
{
    FIXME("%#Ix, %s, %s, %p, stub!\n", instance, debugstr_a(source), debugstr_a(dest), status);
    return JET_errFileNotFound;
}

/***********************************************************************
 * Tables, columns and indexes
 */

static JET_ERR parse_index_key(const struct table *table, const char *key, ULONG len, struct index *index)
{
    const char *p = key, *end = key + len;

    index->segment_count = 0;
    while (p < end && *p)
    {
        BOOL descending = FALSE;
        int col;

        if (*p == '+' || *p == '-')
        {
            descending = *p == '-';
            ++p;
        }
        if ((col = find_column(table, p)) < 0)
        {
            WARN("Column %s not found.\n", debugstr_a(p));
            return JET_errColumnNotFound;
        }
        if (index->segment_count == MAX_SEGMENTS) return JET_errInvalidParameter;
        index->segments[index->segment_count].column = col;
        index->segments[index->segment_count++].descending = descending;
        p += strlen(p) + 1;
    }
    return index->segment_count ? JET_errSuccess : JET_errInvalidParameter;
}

static JET_ERR add_index(struct table *table, const JET_INDEXCREATE_A *create)
{
    struct index *indexes, index = { 0 };
    JET_ERR ret;

    TRACE("  index %s, grbit %#lx, key %s.\n", debugstr_a(create->szIndexName), create->grbit,
            debugstr_an(create->szKey, create->cbKey));

    if (find_index(table, create->szIndexName) >= 0) return JET_errIndexDuplicate;

    copy_name(index.name, create->szIndexName);
    index.grbit = create->grbit;
    if ((ret = parse_index_key(table, create->szKey, create->cbKey, &index))) return ret;

    if (!(indexes = realloc(table->indexes, (table->index_count + 1) * sizeof(*indexes)))) return JET_errOutOfMemory;
    table->indexes = indexes;
    table->indexes[table->index_count++] = index;
    table_changed(table);
    return JET_errSuccess;
}

static JET_ERR add_column(struct table *table, const JET_COLUMNCREATE_A *create)
{
    struct column *columns, col = { 0 };
    unsigned int i;

    TRACE("  column %s, coltyp %lu, cbMax %lu, grbit %#lx, cbDefault %lu, cp %lu.\n",
            debugstr_a(create->szColumnName), create->coltyp, create->cbMax, create->grbit,
            create->cbDefault, create->cp);

    if (find_column(table, create->szColumnName) >= 0) return JET_errColumnDuplicate;

    copy_name(col.name, create->szColumnName);
    col.type = create->coltyp;
    col.cb_max = create->cbMax;
    col.grbit = create->grbit;
    col.cp = create->cp;
    if (is_text(&col) && col.cp != 1200) col.cp = 1252;
    if (create->pvDefault && create->cbDefault && !value_set(&col.def, create->pvDefault, create->cbDefault))
        return JET_errOutOfMemory;

    if (col.grbit & (JET_bitColumnTagged | JET_bitColumnMultiValued)
            || col.type == JET_coltypLongText || col.type == JET_coltypLongBinary)
        col.id = table->next_tagged++;
    else if (col.grbit & JET_bitColumnFixed
            || (col.type != JET_coltypText && col.type != JET_coltypBinary))
        col.id = table->next_fixed++;
    else
        col.id = table->next_variable++;

    if (!(columns = realloc(table->columns, (table->column_count + 1) * sizeof(*columns))))
    {
        free(col.def.data);
        return JET_errOutOfMemory;
    }
    table->columns = columns;
    table->columns[table->column_count] = col;

    /* existing records get an empty value list for the new column */
    for (i = 0; i < table->record_count; ++i)
    {
        struct values *vals = realloc(table->records[i]->columns, (table->column_count + 1) * sizeof(*vals));
        if (!vals) return JET_errOutOfMemory;
        memset(&vals[table->column_count], 0, sizeof(*vals));
        table->records[i]->columns = vals;
    }
    ++table->column_count;
    table_changed(table);
    return JET_errSuccess;
}

static struct cursor *create_cursor(struct session *session, struct database *db, struct table *table)
{
    struct cursor *cursor;

    if (!(cursor = calloc(1, sizeof(*cursor)))) return NULL;
    cursor->session = session;
    cursor->db = db;
    cursor->table = table;
    cursor->index = primary_index(table);
    return cursor;
}

JET_ERR JET_API JetCreateTableColumnIndex2(JET_SESID sesid, JET_DBID dbid, JET_TABLECREATE2_A *create)
{
    struct session *session = get_session(sesid);
    struct database *db = get_database(session, dbid);
    struct table *table, **tables;
    struct cursor *cursor;
    JET_ERR ret = JET_errSuccess;
    unsigned int i;

    TRACE("%#Ix, %lu, %p.\n", sesid, dbid, create);

    if (!db) return JET_errInvalidDatabaseId;
    if (!create || !create->szTableName) return JET_errInvalidParameter;

    TRACE("table %s, %lu columns, %lu indexes, grbit %#lx, template %s, callback %s.\n",
            debugstr_a(create->szTableName), create->cColumns, create->cIndexes, create->grbit,
            debugstr_a(create->szTemplateTableName), debugstr_a(create->szCallback));
    if (create->szTemplateTableName) FIXME("Template tables are not implemented.\n");

    EnterCriticalSection(&esent_cs);
    if (find_table(db, create->szTableName))
    {
        LeaveCriticalSection(&esent_cs);
        return JET_errTableDuplicate;
    }
    if (!(table = calloc(1, sizeof(*table))))
    {
        LeaveCriticalSection(&esent_cs);
        return JET_errOutOfMemory;
    }
    copy_name(table->name, create->szTableName);
    table->next_fixed = 1;
    table->next_variable = 128;
    table->next_tagged = 256;
    table->next_record_id = 1;
    table->next_autoinc = 1;
    create->cCreated = 0;

    for (i = 0; i < create->cColumns && !ret; ++i)
    {
        JET_COLUMNCREATE_A *col = &create->rgcolumncreate[i];
        if ((col->err = ret = add_column(table, col))) break;
        col->columnid = table->columns[table->column_count - 1].id;
        ++create->cCreated;
    }
    for (i = 0; i < create->cIndexes && !ret; ++i)
    {
        JET_INDEXCREATE_A *idx = &create->rgindexcreate[i];
        if ((idx->err = ret = add_index(table, idx))) break;
        ++create->cCreated;
    }

    if (!ret && (tables = realloc(db->tables, (db->table_count + 1) * sizeof(*tables))))
    {
        db->tables = tables;
        db->tables[db->table_count++] = table;
        ++create->cCreated;
        if ((cursor = create_cursor(session, db, table)))
            create->tableid = (JET_TABLEID)cursor;
        else
            ret = JET_errOutOfMemory;
    }
    else
    {
        if (!ret) ret = JET_errOutOfMemory;
        table_free(table);
    }
    LeaveCriticalSection(&esent_cs);
    TRACE("returning %ld, tableid %#Ix.\n", ret, create->tableid);
    return ret;
}

JET_ERR JET_API JetCreateIndex2(JET_SESID sesid, JET_TABLEID tableid, JET_INDEXCREATE_A *create, unsigned long count)
{
    struct cursor *cursor = get_cursor(tableid);
    JET_ERR ret = JET_errSuccess;
    unsigned int i;

    TRACE("%#Ix, %#Ix, %p, %lu.\n", sesid, tableid, create, count);

    if (!cursor) return JET_errInvalidTableId;
    EnterCriticalSection(&esent_cs);
    for (i = 0; i < count && !ret; ++i)
        create[i].err = ret = add_index(cursor->table, &create[i]);
    LeaveCriticalSection(&esent_cs);
    return ret;
}

JET_ERR JET_API JetOpenTable(JET_SESID sesid, JET_DBID dbid, JET_PCSTR name, const void *params,
        unsigned long params_size, JET_GRBIT grbit, JET_TABLEID *tableid)
{
    struct session *session = get_session(sesid);
    struct database *db = get_database(session, dbid);
    struct cursor *cursor;
    struct table *table;

    TRACE("%#Ix, %lu, %s, %p, %lu, %#lx, %p.\n", sesid, dbid, debugstr_a(name), params, params_size, grbit, tableid);

    if (!db) return JET_errInvalidDatabaseId;
    if (!name || !tableid) return JET_errInvalidParameter;

    EnterCriticalSection(&esent_cs);
    if (!(table = find_table(db, name)))
    {
        LeaveCriticalSection(&esent_cs);
        TRACE("Table %s not found.\n", debugstr_a(name));
        return JET_errObjectNotFound;
    }
    cursor = create_cursor(session, db, table);
    LeaveCriticalSection(&esent_cs);
    if (!cursor) return JET_errOutOfMemory;
    *tableid = (JET_TABLEID)cursor;
    return JET_errSuccess;
}

JET_ERR JET_API JetCloseTable(JET_SESID sesid, JET_TABLEID tableid)
{
    struct cursor *cursor = get_cursor(tableid);

    TRACE("%#Ix, %#Ix.\n", sesid, tableid);

    if (!cursor) return JET_errInvalidTableId;
    EnterCriticalSection(&esent_cs);
    free_cursor(cursor);
    LeaveCriticalSection(&esent_cs);
    return JET_errSuccess;
}

static void fill_column_def(const struct column *col, JET_COLUMNDEF *def)
{
    def->cbStruct = sizeof(*def);
    def->columnid = col->id;
    def->coltyp = col->type;
    def->wCountry = 1;
    def->langid = 0x409;
    def->cp = col->cp;
    def->wCollate = 0;
    def->cbMax = col->cb_max;
    def->grbit = col->grbit;
}

static JET_ERR get_column_info(const struct table *table, const void *column, void *result,
        unsigned long size, unsigned long level)
{
    int col;

    switch (level)
    {
    case JET_ColInfo:
    case JET_ColInfoByColid:
        col = level == JET_ColInfoByColid ? find_column_id(table, *(JET_COLUMNID *)column) : find_column(table, column);
        if (col < 0) return JET_errColumnNotFound;
        if (size < sizeof(JET_COLUMNDEF)) return JET_errInvalidBufferSize;
        fill_column_def(&table->columns[col], result);
        return JET_errSuccess;

    case JET_ColInfoBase:
    {
        JET_COLUMNBASE_A *base = result;

        if ((col = find_column(table, column)) < 0) return JET_errColumnNotFound;
        if (size < sizeof(*base)) return JET_errInvalidBufferSize;
        memset(base, 0, sizeof(*base));
        base->cbStruct = sizeof(*base);
        base->columnid = table->columns[col].id;
        base->coltyp = table->columns[col].type;
        base->wCountry = 1;
        base->langid = 0x409;
        base->cp = table->columns[col].cp;
        base->cbMax = table->columns[col].cb_max;
        base->grbit = table->columns[col].grbit;
        lstrcpynA(base->szBaseTableName, table->name, sizeof(base->szBaseTableName));
        lstrcpynA(base->szBaseColumnName, table->columns[col].name, sizeof(base->szBaseColumnName));
        return JET_errSuccess;
    }
    }
    FIXME("Unhandled level %lu.\n", level);
    return JET_errFeatureNotAvailable;
}

JET_ERR JET_API JetGetTableColumnInfo(JET_SESID sesid, JET_TABLEID tableid, JET_PCSTR column, void *result,
        unsigned long size, unsigned long level)
{
    struct cursor *cursor = get_cursor(tableid);
    JET_ERR ret;

    TRACE("%#Ix, %#Ix, %s, %p, %lu, %lu.\n", sesid, tableid,
            level == JET_ColInfoByColid ? "(by id)" : debugstr_a(column), result, size, level);

    if (!cursor) return JET_errInvalidTableId;
    EnterCriticalSection(&esent_cs);
    ret = get_column_info(cursor->table, column, result, size, level);
    LeaveCriticalSection(&esent_cs);
    return ret;
}

JET_ERR JET_API JetGetColumnInfo(JET_SESID sesid, JET_DBID dbid, JET_PCSTR table_name, JET_PCSTR column,
        void *result, unsigned long size, unsigned long level)
{
    struct database *db = get_database(get_session(sesid), dbid);
    struct table *table;
    JET_ERR ret;

    TRACE("%#Ix, %lu, %s, %s, %p, %lu, %lu.\n", sesid, dbid, debugstr_a(table_name),
            level == JET_ColInfoByColid ? "(by id)" : debugstr_a(column), result, size, level);

    if (!db) return JET_errInvalidDatabaseId;
    EnterCriticalSection(&esent_cs);
    if ((table = find_table(db, table_name)))
        ret = get_column_info(table, column, result, size, level);
    else
        ret = JET_errObjectNotFound;
    LeaveCriticalSection(&esent_cs);
    return ret;
}

JET_ERR JET_API JetGetTableIndexInfo(JET_SESID sesid, JET_TABLEID tableid, JET_PCSTR index, void *result,
        unsigned long size, unsigned long level)
{
    struct cursor *cursor = get_cursor(tableid);
    JET_ERR ret = JET_errSuccess;
    int idx;

    TRACE("%#Ix, %#Ix, %s, %p, %lu, %lu.\n", sesid, tableid, debugstr_a(index), result, size, level);

    if (!cursor) return JET_errInvalidTableId;
    EnterCriticalSection(&esent_cs);
    switch (level)
    {
    case JET_IdxInfoCount:
        if (size < sizeof(ULONG)) ret = JET_errInvalidBufferSize;
        else *(ULONG *)result = cursor->table->index_count;
        break;
    case JET_IdxInfoLangid:
        if (index && (idx = find_index(cursor->table, index)) < 0) ret = JET_errIndexNotFound;
        else if (size < sizeof(USHORT)) ret = JET_errInvalidBufferSize;
        else *(USHORT *)result = 0x409;
        break;
    case JET_IdxInfoVarSegMac:
        if (index && (idx = find_index(cursor->table, index)) < 0) ret = JET_errIndexNotFound;
        else if (size < sizeof(USHORT)) ret = JET_errInvalidBufferSize;
        else *(USHORT *)result = JET_cbKeyMost;
        break;
    default:
        FIXME("Unhandled level %lu.\n", level);
        ret = JET_errFeatureNotAvailable;
    }
    LeaveCriticalSection(&esent_cs);
    return ret;
}

JET_ERR JET_API JetSetCurrentIndex(JET_SESID sesid, JET_TABLEID tableid, JET_PCSTR name)
{
    struct cursor *cursor = get_cursor(tableid);
    int index;

    TRACE("%#Ix, %#Ix, %s.\n", sesid, tableid, debugstr_a(name));

    if (!cursor) return JET_errInvalidTableId;
    EnterCriticalSection(&esent_cs);
    if (!name || !*name)
        index = primary_index(cursor->table);
    else if ((index = find_index(cursor->table, name)) < 0)
    {
        LeaveCriticalSection(&esent_cs);
        return JET_errIndexNotFound;
    }
    if (index != cursor->index)
    {
        cursor->index = index;
        free(cursor->entries);
        cursor->entries = NULL;
        cursor->range = FALSE;
        cursor->key_made = FALSE;
        /* ESENT moves to the first record of the new index */
        cursor->on_record = FALSE;
        cursor->pos = 0;
        cursor_update_entries(cursor);
        if (cursor->entry_count) cursor_set_position(cursor, 0);
    }
    LeaveCriticalSection(&esent_cs);
    return JET_errSuccess;
}

/***********************************************************************
 * Navigation
 */

JET_ERR JET_API JetMove(JET_SESID sesid, JET_TABLEID tableid, long rows, JET_GRBIT grbit)
{
    struct cursor *cursor = get_cursor(tableid);
    JET_ERR ret = JET_errSuccess;
    long pos;

    TRACE("%#Ix, %#Ix, %ld, %#lx.\n", sesid, tableid, rows, grbit);

    if (!cursor) return JET_errInvalidTableId;
    EnterCriticalSection(&esent_cs);
    if (!cursor_update_entries(cursor))
    {
        LeaveCriticalSection(&esent_cs);
        return JET_errOutOfMemory;
    }

    if (rows == JET_MoveFirst)
    {
        cursor->range = FALSE;
        pos = 0;
    }
    else if (rows == JET_MoveLast)
    {
        cursor->range = FALSE;
        pos = (long)cursor->entry_count - 1;
    }
    else if (cursor->on_record)
        pos = (long)cursor->pos + rows;
    else
        pos = (long)cursor->pos + (rows > 0 ? rows - 1 : rows);

    if (pos < 0 || pos >= (long)cursor->entry_count || !entry_in_range(cursor, pos))
    {
        /* off the end: ESENT stays before the first or after the last record */
        cursor->on_record = FALSE;
        cursor->current = NULL;
        cursor->pos = pos < 0 ? 0 : cursor->entry_count;
        ret = JET_errNoCurrentRecord;
    }
    else cursor_set_position(cursor, pos);
    LeaveCriticalSection(&esent_cs);
    if (ret) TRACE("returning %ld.\n", ret);
    return ret;
}

JET_ERR JET_API JetMakeKey(JET_SESID sesid, JET_TABLEID tableid, const void *data, unsigned long size, JET_GRBIT grbit)
{
    struct cursor *cursor = get_cursor(tableid);
    struct key *key;

    TRACE("%#Ix, %#Ix, %p, %lu, %#lx.\n", sesid, tableid, data, size, grbit);

    if (!cursor) return JET_errInvalidTableId;
    if (cursor->index < 0) return JET_errInvalidParameter;
    key = &cursor->key;

    EnterCriticalSection(&esent_cs);
    if (grbit & JET_bitNormalizedKey)
    {
        /* our "normalized" keys are the serialized segments from JetRetrieveKey() */
        const BYTE *p = data, *end = p + size;
        unsigned int i;

        for (i = 0; i < key->count; ++i) free(key->segments[i].data);
        memset(key, 0, sizeof(*key));
        while (p + sizeof(ULONG) + 1 <= end && key->count < MAX_SEGMENTS)
        {
            ULONG len;
            key->null[key->count] = *p++;
            memcpy(&len, p, sizeof(len));
            p += sizeof(len);
            if (p + len > end) break;
            value_set(&key->segments[key->count++], p, len);
            p += len;
        }
        cursor->key_made = TRUE;
        LeaveCriticalSection(&esent_cs);
        return JET_errSuccess;
    }

    if (grbit & JET_bitNewKey)
    {
        unsigned int i;
        for (i = 0; i < key->count; ++i) free(key->segments[i].data);
        memset(key, 0, sizeof(*key));
        cursor->key_made = TRUE;
    }
    else if (!cursor->key_made)
    {
        LeaveCriticalSection(&esent_cs);
        return JET_errKeyNotMade;
    }

    if (key->count >= cursor->table->indexes[cursor->index].segment_count)
    {
        LeaveCriticalSection(&esent_cs);
        return JET_errInvalidParameter;
    }

    if (!data && !size && !(grbit & JET_bitKeyDataZeroLength))
        key->null[key->count] = TRUE;
    else
        value_set(&key->segments[key->count], data, size);
    ++key->count;

    if (grbit & (JET_bitStrLimit | JET_bitSubStrLimit | JET_bitPartialColumnEndLimit))
    {
        key->prefix = TRUE;
        key->limit = 1;
    }
    else if (grbit & JET_bitPartialColumnStartLimit)
    {
        key->prefix = TRUE;
        key->limit = -1;
    }
    else if (grbit & JET_bitFullColumnEndLimit)
        key->limit = 1;
    else if (grbit & JET_bitFullColumnStartLimit)
        key->limit = -1;
    LeaveCriticalSection(&esent_cs);
    return JET_errSuccess;
}

JET_ERR JET_API JetSeek(JET_SESID sesid, JET_TABLEID tableid, JET_GRBIT grbit)
{
    struct cursor *cursor = get_cursor(tableid);
    const struct index *index;
    JET_ERR ret = JET_errRecordNotFound;
    struct key search, key;
    unsigned int i;
    int cmp;

    TRACE("%#Ix, %#Ix, %#lx.\n", sesid, tableid, grbit);

    if (!cursor) return JET_errInvalidTableId;
    if (!cursor->key_made) return JET_errKeyNotMade;

    EnterCriticalSection(&esent_cs);
    if (!cursor_update_entries(cursor))
    {
        LeaveCriticalSection(&esent_cs);
        return JET_errOutOfMemory;
    }
    index = &cursor->table->indexes[cursor->index];
    search = cursor->key;

    if (grbit & (JET_bitSeekGE | JET_bitSeekGT))
    {
        for (i = 0; i < cursor->entry_count; ++i)
        {
            entry_key(cursor->table, index, &cursor->entries[i], &key);
            cmp = compare_keys(cursor->table, index, &key, &search);
            if (cmp > 0 || (cmp == 0 && (grbit & JET_bitSeekGE)))
            {
                search.limit = 0;
                ret = compare_keys(cursor->table, index, &key, &search) ? JET_wrnSeekNotEqual : JET_errSuccess;
                if (grbit & JET_bitSeekGT) ret = JET_errSuccess;
                break;
            }
        }
    }
    else if (grbit & (JET_bitSeekLE | JET_bitSeekLT))
    {
        for (i = cursor->entry_count; i-- > 0;)
        {
            entry_key(cursor->table, index, &cursor->entries[i], &key);
            cmp = compare_keys(cursor->table, index, &key, &search);
            if (cmp < 0 || (cmp == 0 && (grbit & JET_bitSeekLE)))
            {
                search.limit = 0;
                ret = compare_keys(cursor->table, index, &key, &search) ? JET_wrnSeekNotEqual : JET_errSuccess;
                if (grbit & JET_bitSeekLT) ret = JET_errSuccess;
                break;
            }
        }
        if (i == ~0u) i = cursor->entry_count;
    }
    else
    {
        /* JET_bitSeekEQ: the first entry matching all given segments */
        search.limit = 0;
        for (i = 0; i < cursor->entry_count; ++i)
        {
            struct key partial;
            entry_key(cursor->table, index, &cursor->entries[i], &key);
            partial = key;
            partial.count = min(key.count, search.count);
            if (!compare_keys(cursor->table, index, &partial, &search))
            {
                ret = JET_errSuccess;
                break;
            }
        }
        if (ret == JET_errSuccess && (grbit & JET_bitSetIndexRange))
        {
            cursor->range = TRUE;
            cursor->range_key = cursor->key;
            cursor->range_key.prefix = TRUE;
            cursor->range_grbit = JET_bitRangeUpperLimit | JET_bitRangeInclusive;
        }
    }

    if (ret >= 0 && i < cursor->entry_count)
        cursor_set_position(cursor, i);
    else
        ret = JET_errRecordNotFound;
    LeaveCriticalSection(&esent_cs);
    TRACE("returning %ld.\n", ret);
    return ret;
}

JET_ERR JET_API JetSetIndexRange(JET_SESID sesid, JET_TABLEID tableid, JET_GRBIT grbit)
{
    struct cursor *cursor = get_cursor(tableid);
    JET_ERR ret = JET_errSuccess;

    TRACE("%#Ix, %#Ix, %#lx.\n", sesid, tableid, grbit);

    if (!cursor) return JET_errInvalidTableId;
    EnterCriticalSection(&esent_cs);
    if (grbit & JET_bitRangeRemove)
        cursor->range = FALSE;
    else if (!cursor->key_made)
        ret = JET_errKeyNotMade;
    else
    {
        cursor->range = TRUE;
        cursor->range_key = cursor->key;
        cursor->range_grbit = grbit;
        if (!cursor_update_entries(cursor)) ret = JET_errOutOfMemory;
        else if (!cursor->on_record || !entry_in_range(cursor, cursor->pos)) ret = JET_errNoCurrentRecord;
    }
    LeaveCriticalSection(&esent_cs);
    return ret;
}

JET_ERR JET_API JetRetrieveKey(JET_SESID sesid, JET_TABLEID tableid, void *data, unsigned long size,
        unsigned long *actual, JET_GRBIT grbit)
{
    struct cursor *cursor = get_cursor(tableid);
    struct key key;
    ULONG needed = 0, i;
    BYTE *p = data;

    TRACE("%#Ix, %#Ix, %p, %lu, %p, %#lx.\n", sesid, tableid, data, size, actual, grbit);

    if (!cursor) return JET_errInvalidTableId;
    if (cursor->index < 0) return JET_errNoCurrentRecord;

    EnterCriticalSection(&esent_cs);
    if (grbit & JET_bitRetrieveCopy)
        key = cursor->key;
    else if (!cursor_record(cursor))
    {
        LeaveCriticalSection(&esent_cs);
        return JET_errNoCurrentRecord;
    }
    else entry_key(cursor->table, &cursor->table->indexes[cursor->index], &cursor->entries[cursor->pos], &key);

    for (i = 0; i < key.count; ++i) needed += 1 + sizeof(ULONG) + key.segments[i].len;
    if (actual) *actual = needed;
    for (i = 0; i < key.count && p; ++i)
    {
        ULONG len = key.segments[i].len;
        if ((ULONG)(p - (BYTE *)data) + 1 + sizeof(ULONG) + len > size) break;
        *p++ = key.null[i];
        memcpy(p, &len, sizeof(len));
        p += sizeof(len);
        memcpy(p, key.segments[i].data, len);
        p += len;
    }
    LeaveCriticalSection(&esent_cs);
    return needed > size ? JET_wrnBufferTruncated : JET_errSuccess;
}

JET_ERR JET_API JetGetRecordPosition(JET_SESID sesid, JET_TABLEID tableid, JET_RECPOS *recpos, unsigned long size)
{
    struct cursor *cursor = get_cursor(tableid);

    TRACE("%#Ix, %#Ix, %p, %lu.\n", sesid, tableid, recpos, size);

    if (!cursor) return JET_errInvalidTableId;
    if (size < sizeof(*recpos)) return JET_errInvalidBufferSize;
    EnterCriticalSection(&esent_cs);
    if (!cursor_record(cursor))
    {
        LeaveCriticalSection(&esent_cs);
        return JET_errNoCurrentRecord;
    }
    recpos->cbStruct = sizeof(*recpos);
    recpos->centriesLT = cursor->pos;
    recpos->centriesInRange = 1;
    recpos->centriesTotal = cursor->entry_count;
    LeaveCriticalSection(&esent_cs);
    return JET_errSuccess;
}

/***********************************************************************
 * Columns
 */

/* Returns the values of a column, from the copy buffer or the current record. */
static JET_ERR cursor_column(struct cursor *cursor, JET_COLUMNID columnid, BOOL copy,
        struct values **vals, const struct column **column)
{
    struct record *rec;
    int col;

    if ((col = find_column_id(cursor->table, columnid)) < 0) return JET_errColumnNotFound;
    if (copy)
    {
        if (!cursor->prepared) return JET_errUpdateNotPrepared;
        rec = cursor->copy;
    }
    else if (!(rec = cursor_record(cursor))) return JET_errNoCurrentRecord;
    *vals = &rec->columns[col];
    *column = &cursor->table->columns[col];
    return JET_errSuccess;
}

static JET_ERR retrieve_value(struct cursor *cursor, JET_COLUMNID columnid, void *data, unsigned long size,
        unsigned long *actual, JET_GRBIT grbit, ULONG offset, ULONG tag)
{
    const struct column *column;
    const struct value *value;
    struct values *vals;
    ULONG len;
    JET_ERR ret;

    if ((ret = cursor_column(cursor, columnid, !!(grbit & JET_bitRetrieveCopy), &vals, &column)))
        return ret;

    if (!tag) tag = 1;
    if (tag <= vals->count)
        value = &vals->items[tag - 1];
    else if (tag == 1 && !vals->count && column->def.len && !(grbit & JET_bitRetrieveIgnoreDefault))
        value = &column->def;
    else
    {
        if (actual) *actual = 0;
        return JET_wrnColumnNull;
    }

    len = offset < value->len ? value->len - offset : 0;
    if (actual) *actual = len;
    if (data && size) memcpy(data, value->data + offset, min(len, size));
    return len > size ? JET_wrnBufferTruncated : JET_errSuccess;
}

JET_ERR JET_API JetRetrieveColumn(JET_SESID sesid, JET_TABLEID tableid, JET_COLUMNID columnid, void *data,
        unsigned long size, unsigned long *actual, JET_GRBIT grbit, JET_RETINFO *info)
{
    struct cursor *cursor = get_cursor(tableid);
    JET_ERR ret;

    TRACE("%#Ix, %#Ix, %lu, %p, %lu, %p, %#lx, %p.\n", sesid, tableid, columnid, data, size, actual, grbit, info);

    if (!cursor) return JET_errInvalidTableId;
    EnterCriticalSection(&esent_cs);
    ret = retrieve_value(cursor, columnid, data, size, actual, grbit,
            info ? info->ibLongValue : 0, info ? info->itagSequence : 1);
    if (info) info->columnidNextTagged = 0;
    LeaveCriticalSection(&esent_cs);
    return ret;
}

JET_ERR JET_API JetRetrieveColumns(JET_SESID sesid, JET_TABLEID tableid, JET_RETRIEVECOLUMN *columns,
        unsigned long count)
{
    struct cursor *cursor = get_cursor(tableid);
    unsigned long i;
    JET_ERR ret = JET_errSuccess;

    TRACE("%#Ix, %#Ix, %p, %lu.\n", sesid, tableid, columns, count);

    if (!cursor) return JET_errInvalidTableId;
    EnterCriticalSection(&esent_cs);
    for (i = 0; i < count; ++i)
    {
        JET_RETRIEVECOLUMN *c = &columns[i];

        c->err = retrieve_value(cursor, c->columnid, c->pvData, c->cbData, &c->cbActual, c->grbit,
                c->ibLongValue, c->itagSequence);
        c->columnidNextTagged = 0;
        if (c->err < 0)
        {
            ret = c->err;
            break;
        }
        if (c->err > 0 && ret == JET_errSuccess) ret = c->err;
    }
    LeaveCriticalSection(&esent_cs);
    return ret;
}

static JET_ERR set_value(struct cursor *cursor, JET_COLUMNID columnid, const void *data, unsigned long size,
        JET_GRBIT grbit, ULONG offset, ULONG tag)
{
    const struct column *column;
    struct values *vals;
    struct value *value;
    JET_ERR ret;

    if ((ret = cursor_column(cursor, columnid, TRUE, &vals, &column))) return ret;
    if (column->cb_max && size > column->cb_max && !(grbit & (JET_bitSetAppendLV | JET_bitSetOverwriteLV)))
        WARN("Value of %lu bytes is larger than cbMax %lu.\n", size, column->cb_max);

    if (grbit & JET_bitSetRevertToDefaultValue)
    {
        values_clear(vals);
        return JET_errSuccess;
    }

    /* tag 0 adds a new value to a multi-valued column */
    if (!tag)
        tag = (column->grbit & JET_bitColumnMultiValued) ? vals->count + 1 : 1;

    if (!data && !size && !(grbit & JET_bitSetZeroLength))
    {
        /* setting NULL removes the value */
        if (tag <= vals->count)
        {
            free(vals->items[tag - 1].data);
            memmove(&vals->items[tag - 1], &vals->items[tag], (vals->count - tag) * sizeof(*vals->items));
            --vals->count;
        }
        return JET_errSuccess;
    }

    if ((grbit & JET_bitSetUniqueMultiValues) && (column->grbit & JET_bitColumnMultiValued))
    {
        ULONG i;
        for (i = 0; i < vals->count; ++i)
            if (vals->items[i].len == size && !memcmp(vals->items[i].data, data, size))
                return JET_errSuccess;
    }

    if (tag > vals->count)
    {
        struct value *items = realloc(vals->items, (vals->count + 1) * sizeof(*items));
        if (!items) return JET_errOutOfMemory;
        vals->items = items;
        memset(&items[vals->count], 0, sizeof(*items));
        tag = ++vals->count;
    }
    value = &vals->items[tag - 1];

    if (grbit & (JET_bitSetAppendLV | JET_bitSetOverwriteLV | JET_bitSetSizeLV))
    {
        ULONG start = (grbit & JET_bitSetAppendLV) ? value->len : offset;
        ULONG new_len = (grbit & JET_bitSetSizeLV) ? size : max(value->len, start + size);
        BYTE *buf;

        if (!(buf = realloc(value->data, max(new_len, 1)))) return JET_errOutOfMemory;
        if (new_len > value->len) memset(buf + value->len, 0, new_len - value->len);
        if (!(grbit & JET_bitSetSizeLV) && size) memcpy(buf + start, data, size);
        value->data = buf;
        value->len = new_len;
        return JET_errSuccess;
    }

    return value_set(value, data, size) ? JET_errSuccess : JET_errOutOfMemory;
}

JET_ERR JET_API JetSetColumn(JET_SESID sesid, JET_TABLEID tableid, JET_COLUMNID columnid, const void *data,
        unsigned long size, JET_GRBIT grbit, JET_SETINFO *info)
{
    struct cursor *cursor = get_cursor(tableid);
    JET_ERR ret;

    TRACE("%#Ix, %#Ix, %lu, %p, %lu, %#lx, %p.\n", sesid, tableid, columnid, data, size, grbit, info);

    if (!cursor) return JET_errInvalidTableId;
    EnterCriticalSection(&esent_cs);
    ret = set_value(cursor, columnid, data, size, grbit, info ? info->ibLongValue : 0, info ? info->itagSequence : 1);
    LeaveCriticalSection(&esent_cs);
    return ret;
}

JET_ERR JET_API JetSetColumns(JET_SESID sesid, JET_TABLEID tableid, JET_SETCOLUMN *columns, unsigned long count)
{
    struct cursor *cursor = get_cursor(tableid);
    unsigned long i;
    JET_ERR ret = JET_errSuccess;

    TRACE("%#Ix, %#Ix, %p, %lu.\n", sesid, tableid, columns, count);

    if (!cursor) return JET_errInvalidTableId;
    EnterCriticalSection(&esent_cs);
    for (i = 0; i < count; ++i)
    {
        JET_SETCOLUMN *c = &columns[i];

        if ((c->err = set_value(cursor, c->columnid, c->pvData, c->cbData, c->grbit, c->ibLongValue,
                c->itagSequence)) < 0)
        {
            ret = c->err;
            break;
        }
    }
    LeaveCriticalSection(&esent_cs);
    return ret;
}

JET_ERR JET_API JetEnumerateColumns(JET_SESID sesid, JET_TABLEID tableid, unsigned long id_count,
        JET_ENUMCOLUMNID *ids, unsigned long *count, JET_ENUMCOLUMN **columns, JET_PFNREALLOC realloc_func,
        void *context, unsigned long data_max, JET_GRBIT grbit)
{
    struct cursor *cursor = get_cursor(tableid);
    JET_ENUMCOLUMN *out = NULL;
    struct record *rec;
    unsigned long n = 0, i;
    JET_ERR ret = JET_errSuccess;

    TRACE("%#Ix, %#Ix, %lu, %p, %p, %p, %p, %p, %lu, %#lx.\n", sesid, tableid, id_count, ids, count, columns,
            realloc_func, context, data_max, grbit);

    if (!cursor) return JET_errInvalidTableId;
    if (!count || !columns || !realloc_func) return JET_errInvalidParameter;
    if (grbit & JET_bitEnumerateCompressOutput)
        FIXME("JET_bitEnumerateCompressOutput is not implemented.\n");

    EnterCriticalSection(&esent_cs);
    if (grbit & JET_bitEnumerateCopy)
        rec = cursor->prepared ? cursor->copy : NULL;
    else
        rec = cursor_record(cursor);
    if (!rec)
    {
        LeaveCriticalSection(&esent_cs);
        return (grbit & JET_bitEnumerateCopy) ? JET_errUpdateNotPrepared : JET_errNoCurrentRecord;
    }

    if (!(out = realloc_func(context, NULL, max(id_count ? id_count : cursor->table->column_count, 1) * sizeof(*out))))
    {
        LeaveCriticalSection(&esent_cs);
        return JET_errOutOfMemory;
    }

    for (i = 0; i < (id_count ? id_count : cursor->table->column_count); ++i)
    {
        const struct column *column;
        const struct values *vals;
        JET_ENUMCOLUMN *c;
        ULONG v;
        int col;

        if (id_count)
        {
            if ((col = find_column_id(cursor->table, ids[i].columnid)) < 0)
            {
                c = &out[n++];
                memset(c, 0, sizeof(*c));
                c->columnid = ids[i].columnid;
                c->err = JET_errColumnNotFound;
                continue;
            }
        }
        else col = i;

        column = &cursor->table->columns[col];
        vals = &rec->columns[col];
        if ((grbit & JET_bitEnumerateTaggedOnly) && !(column->grbit & (JET_bitColumnTagged | JET_bitColumnMultiValued)))
            continue;
        if (!id_count && !vals->count && (!column->def.len || (grbit & JET_bitEnumerateIgnoreDefault)))
            continue;

        c = &out[n++];
        memset(c, 0, sizeof(*c));
        c->columnid = column->id;
        if (!vals->count && (!column->def.len || (grbit & JET_bitEnumerateIgnoreDefault)))
        {
            c->err = JET_wrnColumnNull;
            continue;
        }

        c->cEnumColumnValue = vals->count ? vals->count : 1;
        if (!(c->rgEnumColumnValue = realloc_func(context, NULL, c->cEnumColumnValue * sizeof(*c->rgEnumColumnValue))))
        {
            ret = JET_errOutOfMemory;
            break;
        }
        for (v = 0; v < c->cEnumColumnValue; ++v)
        {
            const struct value *value = vals->count ? &vals->items[v] : &column->def;
            JET_ENUMCOLUMNVALUE *ev = &c->rgEnumColumnValue[v];
            ULONG len = min(value->len, data_max ? data_max : value->len);

            ev->itagSequence = v + 1;
            ev->err = len < value->len ? JET_wrnBufferTruncated : JET_errSuccess;
            ev->cbData = value->len;
            ev->pvData = NULL;
            if ((grbit & JET_bitEnumeratePresenceOnly) || !len) continue;
            if (!(ev->pvData = realloc_func(context, NULL, len)))
            {
                ret = JET_errOutOfMemory;
                break;
            }
            memcpy(ev->pvData, value->data, len);
        }
        if (ret) break;
    }
    LeaveCriticalSection(&esent_cs);

    *count = n;
    *columns = out;
    return ret;
}

/***********************************************************************
 * Updates
 */

static JET_ERR check_unique(struct cursor *cursor, const struct record *rec, const struct record *replaced)
{
    struct table *table = cursor->table;
    unsigned int i, j;

    for (i = 0; i < table->index_count; ++i)
    {
        const struct index *index = &table->indexes[i];
        struct entry a = { (struct record *)rec, rec->columns[index->segments[0].column].count ? 1 : 0 };
        struct key ka, kb;

        if (!(index->grbit & (JET_bitIndexUnique | JET_bitIndexPrimary))) continue;
        if (!entry_included(table, index, &a)) continue;
        entry_key(table, index, &a, &ka);
        for (j = 0; j < table->record_count; ++j)
        {
            struct entry b = { table->records[j], table->records[j]->columns[index->segments[0].column].count ? 1 : 0 };

            if (table->records[j] == replaced) continue;
            if (!entry_included(table, index, &b)) continue;
            entry_key(table, index, &b, &kb);
            if (!compare_keys(table, index, &ka, &kb))
            {
                TRACE("Duplicate key in index %s.\n", debugstr_a(index->name));
                return JET_errKeyDuplicate;
            }
        }
    }
    return JET_errSuccess;
}

JET_ERR JET_API JetPrepareUpdate(JET_SESID sesid, JET_TABLEID tableid, unsigned long prep)
{
    struct cursor *cursor = get_cursor(tableid);
    struct table *table;
    struct record *rec;
    unsigned int i;

    TRACE("%#Ix, %#Ix, %lu.\n", sesid, tableid, prep);

    if (!cursor) return JET_errInvalidTableId;
    table = cursor->table;

    EnterCriticalSection(&esent_cs);
    if (prep == JET_prepCancel)
    {
        if (!cursor->prepared)
        {
            LeaveCriticalSection(&esent_cs);
            return JET_errUpdateNotPrepared;
        }
        record_free(cursor->copy, table->column_count);
        cursor->copy = NULL;
        cursor->prepared = FALSE;
        LeaveCriticalSection(&esent_cs);
        return JET_errSuccess;
    }

    if (cursor->prepared)
    {
        LeaveCriticalSection(&esent_cs);
        return JET_errAlreadyPrepared;
    }

    switch (prep)
    {
    case JET_prepInsert:
        if (!(rec = record_create(table))) break;
        for (i = 0; i < table->column_count; ++i)
        {
            if (table->columns[i].grbit & JET_bitColumnAutoincrement)
            {
                ULONG id = table->next_autoinc++;

                rec->columns[i].items = calloc(1, sizeof(*rec->columns[i].items));
                if (rec->columns[i].items)
                {
                    if (table->columns[i].type == JET_coltypLongLong || table->columns[i].type == JET_coltypCurrency)
                    {
                        LONGLONG id64 = id;
                        value_set(&rec->columns[i].items[0], &id64, sizeof(id64));
                    }
                    else value_set(&rec->columns[i].items[0], &id, sizeof(id));
                    rec->columns[i].count = 1;
                }
            }
        }
        cursor->copy = rec;
        break;

    case JET_prepReplace:
    case JET_prepReplaceNoLock:
    case JET_prepInsertCopy:
    case JET_prepInsertCopyDeleteOriginal:
    {
        struct record *current = cursor_record(cursor);

        if (!current)
        {
            LeaveCriticalSection(&esent_cs);
            return JET_errNoCurrentRecord;
        }
        if (!(rec = record_copy(table, current))) break;
        if (prep == JET_prepInsertCopy || prep == JET_prepInsertCopyDeleteOriginal) rec->id = 0;
        cursor->copy = rec;
        break;
    }

    default:
        FIXME("Unhandled prep %lu.\n", prep);
        LeaveCriticalSection(&esent_cs);
        return JET_errInvalidParameter;
    }

    if (!cursor->copy)
    {
        LeaveCriticalSection(&esent_cs);
        return JET_errOutOfMemory;
    }
    cursor->prep = prep;
    cursor->prepared = TRUE;
    LeaveCriticalSection(&esent_cs);
    return JET_errSuccess;
}

JET_ERR JET_API JetUpdate(JET_SESID sesid, JET_TABLEID tableid, void *bookmark, unsigned long size,
        unsigned long *actual)
{
    struct cursor *cursor = get_cursor(tableid);
    struct record *rec, *replaced = NULL;
    struct table *table;
    unsigned int i;
    JET_ERR ret;

    TRACE("%#Ix, %#Ix, %p, %lu, %p.\n", sesid, tableid, bookmark, size, actual);

    if (!cursor) return JET_errInvalidTableId;
    table = cursor->table;

    EnterCriticalSection(&esent_cs);
    if (!cursor->prepared)
    {
        LeaveCriticalSection(&esent_cs);
        return JET_errUpdateNotPrepared;
    }
    rec = cursor->copy;

    if (cursor->prep == JET_prepReplace || cursor->prep == JET_prepReplaceNoLock
            || cursor->prep == JET_prepInsertCopyDeleteOriginal)
    {
        for (i = 0; i < table->record_count; ++i)
            if (table->records[i]->id == (cursor->prep == JET_prepInsertCopyDeleteOriginal ?
                    cursor->current->id : rec->id)) replaced = table->records[i];
        if (!replaced)
        {
            LeaveCriticalSection(&esent_cs);
            return JET_errNoCurrentRecord;
        }
    }

    for (i = 0; i < table->column_count; ++i)
    {
        if ((table->columns[i].grbit & JET_bitColumnNotNULL) && !rec->columns[i].count && !table->columns[i].def.len)
        {
            TRACE("Column %s can't be NULL.\n", debugstr_a(table->columns[i].name));
            LeaveCriticalSection(&esent_cs);
            return JET_errNullInvalid;
        }
    }

    if ((ret = check_unique(cursor, rec, replaced)))
    {
        LeaveCriticalSection(&esent_cs);
        return ret;
    }

    if (replaced)
    {
        for (i = 0; i < table->record_count; ++i)
        {
            if (table->records[i] != replaced) continue;
            rec->id = replaced->id;
            if (cursor->current == replaced) cursor->current = rec;
            table->records[i] = rec;
            log_undo(cursor->session, UNDO_REPLACE, table, 0, replaced);
            break;
        }
    }
    else
    {
        rec->id = table->next_record_id++;
        if (!table_insert_record(table, rec))
        {
            LeaveCriticalSection(&esent_cs);
            return JET_errOutOfMemory;
        }
        log_undo(cursor->session, UNDO_INSERT, table, rec->id, NULL);
    }

    cursor->copy = NULL;
    cursor->prepared = FALSE;
    table_changed(table);

    if (actual) *actual = sizeof(rec->id);
    if (bookmark && size >= sizeof(rec->id)) memcpy(bookmark, &rec->id, sizeof(rec->id));
    LeaveCriticalSection(&esent_cs);
    return JET_errSuccess;
}

JET_ERR JET_API JetDelete(JET_SESID sesid, JET_TABLEID tableid)
{
    struct cursor *cursor = get_cursor(tableid);
    struct table *table;
    struct record *rec;
    unsigned int i;

    TRACE("%#Ix, %#Ix.\n", sesid, tableid);

    if (!cursor) return JET_errInvalidTableId;
    table = cursor->table;

    EnterCriticalSection(&esent_cs);
    if (!(rec = cursor_record(cursor)))
    {
        LeaveCriticalSection(&esent_cs);
        return JET_errNoCurrentRecord;
    }
    for (i = 0; i < table->record_count; ++i)
    {
        if (table->records[i] != rec) continue;
        table_remove_record(table, i);
        break;
    }
    /* the cursor stays where the record was; JetMove() continues from there */
    cursor->on_record = FALSE;
    cursor->current = NULL;
    log_undo(cursor->session, UNDO_DELETE, table, 0, rec);
    table_changed(table);
    LeaveCriticalSection(&esent_cs);
    return JET_errSuccess;
}
