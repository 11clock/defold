#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "dstrings.h"
#include "http_cache.h"
#include "log.h"
#include "sys.h"
#include "hashtable.h"
#include "hash.h"
#include "math.h"
#include "time.h"
#include "mutex.h"
#include "index_pool.h"
#include "array.h"
#include "poolallocator.h"

namespace dmHttpCache
{
    // Magic file header for index file
    const uint32_t MAGIC = 0xCAAAAAAC;
    // Current index file version
    const uint32_t VERSION = 3;

    // Maximum number of cache entry creations in flight
    const uint32_t MAX_CACHE_CREATORS = 16;

    // Index header struct
    struct IndexHeader
    {
        // Magic number, see MAGIC
        uint32_t m_Magic;
        // Index file version number
        uint32_t m_Version;
        // Checksum of the index payload, ie the data that follows the header
        uint64_t m_Checksum;
    };

    /*
     * In-memory representation of a cache entry
     */
    struct Entry
    {
        Entry()
        {
            memset(this, 0, sizeof(*this));
        }
        EntryInfo m_Info;
        uint8_t  m_ReadLockCount : 8;
        uint8_t  m_WriteLock : 1;
    };

    /*
     * Disk (index) representation of a cache entry
     */
    struct FileEntry
    {
        uint64_t m_UriHash;
        // ETag string
        char     m_ETag[MAX_TAG_LEN];
        // Path string
        char     m_URI[256];
        // The content hash is the hash of URI and ETag.
        uint64_t m_IdentifierHash;
        // Last accessed time
        uint64_t m_LastAccessed;
        // Checksum
        uint64_t m_Checksum;
    };

    /*
     * Cache entry creation state
     */
    struct CacheCreator
    {
        char*       m_Filename;
        FILE*       m_File;
        HashState64 m_ChecksumState;
        uint64_t    m_IdentifierHash;
        uint64_t    m_UriHash;
        uint16_t    m_Index;
        uint32_t    m_Error : 1;
    };

    /*
     * The cache database
     */
    struct Cache
    {
        Cache(const char* path, uint64_t max_entry_age)
        {
            m_Path = strdup(path);
            m_MaxCacheEntryAge = max_entry_age;
            m_CacheTable.SetCapacity(11, 32);
            m_Mutex = dmMutex::New();
            m_Policy = CONSISTENCY_POLICY_VERIFY;
            m_StringAllocator = dmPoolAllocator::New(4096);
        }

        ~Cache()
        {
            free(m_Path);
            dmMutex::Delete(m_Mutex);
            dmPoolAllocator::Delete(m_StringAllocator);
        }

        char*                m_Path;
        uint64_t             m_MaxCacheEntryAge;
        dmHashTable64<Entry> m_CacheTable;
        dmMutex::Mutex       m_Mutex;
        dmIndexPool16        m_CacheCreatorsPool;
        dmArray<CacheCreator> m_CacheCreators;
        ConsistencyPolicy    m_Policy;
        dmPoolAllocator::HPool m_StringAllocator;

    };

    void SetDefaultParams(NewParams* params)
    {
        memset(params, 0, sizeof(*params));
        params->m_MaxCacheEntryAge = 60 * 60 * 24 * 5;
    }

    static void HashToString(uint64_t hash, char* str)
    {
        static const char hex_chars[] = "0123456789abcdef";
        for (int i = 0; i < 8; ++i)
        {
            uint32_t x = (hash >> 8 * (7 - i)) & 0xff;
            *str++ = hex_chars[x >> 4];
            *str++ = hex_chars[x & 0xf];
        }
        *str = 0;
    }

    static void ContentFilePath(HCache cache, uint64_t identifier_hash, char* path, int path_len)
    {
        char identifier_string[8 * 2 + 1];
        identifier_string[sizeof(identifier_string)-1] = '\0';
        HashToString(identifier_hash, &identifier_string[0]);
        DM_SNPRINTF(path, path_len, "%s/%c%c/%s",
                    cache->m_Path,
                    identifier_string[0],
                    identifier_string[1],
                    &identifier_string[2]);
    }

    void RemoveCachedContentFile(HCache cache, uint64_t identifier_hash)
    {
        char path[512];
        ContentFilePath(cache, identifier_hash, path, sizeof(path));
        dmSys::Result r = dmSys::Unlink(path);
        if (r != dmSys::RESULT_OK)
        {
            dmLogWarning("Unable to remove %s", path);
        }
    }

    Result Open(NewParams* params, HCache* cache)
    {
        const char* path = params->m_Path;
        struct stat stat_data;
        int ret = stat(path, &stat_data);
        if (ret == 0)
        {
            if ((stat_data.st_mode & S_IFDIR) == 0)
            {
                dmLogError("Unable to use '%s' as http cache directory. Path exists and is not a directory.", path);
                return RESULT_INVALID_PATH;
            }
        }
        else
        {
            dmSys::Result r = dmSys::Mkdir(path, 0755);
            if (r != dmSys::RESULT_OK)
            {
                dmLogError("Unable to create directory '%s'", path);
                return RESULT_IO_ERROR;
            }
        }

        Cache* c = new Cache(path, params->m_MaxCacheEntryAge * 1000000);
        c->m_CacheCreatorsPool.SetCapacity(MAX_CACHE_CREATORS);
        c->m_CacheCreators.SetCapacity(MAX_CACHE_CREATORS);
        c->m_CacheCreators.SetSize(MAX_CACHE_CREATORS);

        for (uint32_t i = 0; i < MAX_CACHE_CREATORS; ++i)
        {
            CacheCreator* h = &c->m_CacheCreators[i];
            memset(h, 0, sizeof(*h));
        }

        char cache_file[512];
        DM_SNPRINTF(cache_file, sizeof(cache_file), "%s/%s", path, "index");
        FILE* f = fopen(cache_file, "rb");
        if (f)
        {
            fseek(f, 0, SEEK_END);
            size_t size = ftell(f);
            fseek(f, 0, SEEK_SET);
            void* buffer = malloc(size);
            fread(buffer, 1, size, f);
            IndexHeader* header = (IndexHeader*) buffer;
            if (size < (sizeof(IndexHeader)))
            {
                dmLogError("Invalid cache index file '%s'. Removing file.", cache_file);
                // We remove the file and return RESULT_OK
                dmSys::Unlink(cache_file);
            }
            else if (header->m_Magic == MAGIC && header->m_Version == VERSION)
            {
                uint64_t checksum = dmHashBuffer64((void*) (((uintptr_t) buffer) + sizeof(IndexHeader)), size - sizeof(IndexHeader));
                if (checksum != header->m_Checksum)
                {
                    dmLogError("Corrupt cache index file '%s'. Removing file.", cache_file);
                }
                else
                {
                    uint32_t n_entries = (size - sizeof(IndexHeader)) / sizeof(FileEntry);
                    FileEntry* entries = (FileEntry*) (((uintptr_t) buffer) + sizeof(IndexHeader));
                    uint32_t capacity = n_entries + 128;
                    c->m_CacheTable.SetCapacity(2 * capacity / 3, capacity);
                    uint64_t current_time = dmTime::GetTime();
                    for (uint32_t i = 0; i < n_entries; ++i)
                    {
                        if (entries[i].m_LastAccessed + c->m_MaxCacheEntryAge >= current_time)
                        {
                            // Keep cache entry, ie within max age
                            Entry e;
                            memcpy(e.m_Info.m_ETag, entries[i].m_ETag, sizeof(e.m_Info.m_ETag));
                            e.m_Info.m_URI = dmPoolAllocator::Duplicate(c->m_StringAllocator, entries[i].m_URI);
                            e.m_Info.m_IdentifierHash = entries[i].m_IdentifierHash;
                            e.m_Info.m_LastAccessed = entries[i].m_LastAccessed;
                            e.m_Info.m_Checksum = entries[i].m_Checksum;
                            c->m_CacheTable.Put(entries[i].m_UriHash, e);
                        }
                        else
                        {
                            // Remove old cache entry
                            RemoveCachedContentFile(c, entries[i].m_IdentifierHash);
                        }
                    }
                }
            }
            else
            {
                if (((uint32_t*) buffer)[0] != MAGIC)
                    dmLogError("Invalid cache index file '%s'. Removing file.", cache_file);
                // We remove the file and return RESULT_OK
                dmSys::Unlink(cache_file);
            }
            free(buffer);
            fclose(f);
        }

        *cache = c;
        return RESULT_OK;
    }

    struct WriteEntryContext
    {
        FILE* m_File;
        bool m_Error;
        const char* m_Filename;
        HashState64 m_HashState;
        WriteEntryContext(FILE* f, const char* file_name)
        {
            m_File = f;
            m_Filename = file_name;
            m_Error = false;
            dmHashInit64(&m_HashState);
        }
    };

    void WriteEntry(WriteEntryContext* context, const uint64_t* key, Entry* entry)
    {
        if (context->m_Error)
            return;

        if(entry->m_WriteLock)
        {
            dmLogWarning("Invalid http cache state. Not yet flushed cache entry (etag: %s).", entry->m_Info.m_ETag);
            return;
        }

        FileEntry file_entry;
        file_entry.m_UriHash = *key;
        memcpy(file_entry.m_ETag, entry->m_Info.m_ETag, sizeof(file_entry.m_ETag));
        memcpy(file_entry.m_URI, entry->m_Info.m_URI, sizeof(file_entry.m_URI));
        file_entry.m_IdentifierHash = entry->m_Info.m_IdentifierHash;
        file_entry.m_LastAccessed = entry->m_Info.m_LastAccessed;
        file_entry.m_Checksum = entry->m_Info.m_Checksum;

        dmHashUpdateBuffer64(&context->m_HashState, &file_entry, sizeof(file_entry));
        size_t n_written = fwrite(&file_entry, 1, sizeof(file_entry), context->m_File);
        if (n_written != sizeof(file_entry))
        {
            dmLogError("Error writing to index file '%s'", context->m_Filename);
            context->m_Error = true;
        }
    }

    Result Close(HCache cache)
    {
        for (uint32_t i = 0; i < MAX_CACHE_CREATORS; ++i)
        {
            CacheCreator* h = &cache->m_CacheCreators[i];
            if (h->m_Filename)
            {
                free(h->m_Filename);
            }

            if (h->m_File)
            {
                fclose(h->m_File);
            }
        }

        char cache_file[512];
        DM_SNPRINTF(cache_file, sizeof(cache_file), "%s/%s", cache->m_Path, "index");
        FILE* f = fopen(cache_file, "wb");
        if (f)
        {
            IndexHeader header;

            header.m_Magic = MAGIC;
            header.m_Version = VERSION;
            header.m_Checksum = 0;
            size_t n_written = fwrite(&header, 1, sizeof(header), f);
            if (n_written != sizeof(header))
            {
                dmLogError("Error writing to index file '%s'", cache_file);
                fclose(f);
                dmSys::Unlink(cache_file);
                return RESULT_IO_ERROR;
            }
            else
            {
                WriteEntryContext context(f, cache_file);
                cache->m_CacheTable.Iterate(&WriteEntry, &context);
                if (context.m_Error)
                {
                    dmLogError("Error writing to index file '%s'", cache_file);
                    fclose(f);
                    dmSys::Unlink(cache_file);
                    return RESULT_IO_ERROR;
                }
                else
                {
                    // Rewrite header with checksum
                    fseek(f, 0, SEEK_SET);
                    header.m_Checksum = dmHashFinal64(&context.m_HashState);
                    size_t n_written = fwrite(&header, 1, sizeof(header), f);
                    if (n_written != sizeof(header))
                    {
                        dmLogError("Error writing to index file '%s'", cache_file);
                        fclose(f);
                        dmSys::Unlink(cache_file);
                        return RESULT_IO_ERROR;
                    }
                    fclose(f);
                }
            }
        }
        else
        {
            dmLogError("Unable to open index file '%s'", cache_file);
        }
        delete cache;
        return RESULT_OK;
    }

    Result Begin(HCache cache, const char* uri, const char* etag, HCacheCreator* cache_creator)
    {
        dmMutex::ScopedLock lock(cache->m_Mutex);
        *cache_creator = 0;

        uint64_t uri_hash = dmHashString64(uri);

        HashState64 hash_state;
        dmHashInit64(&hash_state);
        dmHashUpdateBuffer64(&hash_state, uri, strlen(uri));
        dmHashUpdateBuffer64(&hash_state, etag, strlen(etag));
        uint64_t identifier_hash = dmHashFinal64(&hash_state);

        Entry* entry = cache->m_CacheTable.Get(uri_hash);
        if (entry)
        {
            if (entry->m_Info.m_IdentifierHash == identifier_hash)
            {
                dmLogWarning("Trying to update existing cache entry for uri: '%s' with etag: '%s'.", uri, etag);
                return RESULT_ALREADY_CACHED;
            }
            else
            {
                if (entry->m_ReadLockCount > 0)
                {
                    dmLogWarning("Cache entry for uri: '%s' with etag: '%s' is locked. Cannot update.", uri, etag);
                    return RESULT_LOCKED;
                }
                else if (entry->m_WriteLock)
                {
                    dmLogWarning("Cache entry for uri: '%s' with etag: '%s' is already locked for update.", uri, etag);
                    return RESULT_LOCKED;
                }
            }
        }
        else
        {
            // New entry
            Entry new_entry;
            if (cache->m_CacheTable.Full())
            {
                uint32_t new_capacity = cache->m_CacheTable.Capacity() + 128;
                cache->m_CacheTable.SetCapacity(dmMath::Max(1U, 2 * new_capacity / 3), new_capacity);
            }
            cache->m_CacheTable.Put(uri_hash, new_entry);
        }

        entry = cache->m_CacheTable.Get(uri_hash);
        dmStrlCpy(entry->m_Info.m_ETag, etag, sizeof(entry->m_Info.m_ETag));
        entry->m_Info.m_URI = dmPoolAllocator::Duplicate(cache->m_StringAllocator, uri);
        entry->m_Info.m_IdentifierHash = identifier_hash;
        entry->m_Info.m_LastAccessed = dmTime::GetTime();
        entry->m_WriteLock = 1;

        if (cache->m_CacheCreatorsPool.Remaining() == 0)
        {
            return RESULT_OUT_OF_RESOURCES;
        }
        uint16_t index = cache->m_CacheCreatorsPool.Pop();

        int file_name_len = strlen(cache->m_Path) + 1 /* slash */ + 8 /* tempXXXX */ + 1 /* '\0' */;
        char* file_name = (char*) malloc(file_name_len);
        DM_SNPRINTF(file_name, file_name_len, "%s/temp%04d", cache->m_Path, (int) index);
        FILE* f = fopen(file_name, "wb");
        if (f == 0)
        {
            dmLogError("Unable to open temporary file: '%s'", file_name);
            free(file_name);
            cache->m_CacheCreatorsPool.Push(index);
            return RESULT_IO_ERROR;
        }

        CacheCreator* handle = &cache->m_CacheCreators[index];
        handle->m_Index = index;
        dmHashInit64(&handle->m_ChecksumState);
        handle->m_File = f;
        handle->m_Filename = file_name;
        handle->m_IdentifierHash = identifier_hash;
        handle->m_UriHash = dmHashString64(uri);
        handle->m_Error = 0;
        *cache_creator = handle;

        return RESULT_OK;
    }

    static void FreeCacheCreator(HCache cache, HCacheCreator cache_creator)
    {
        if (cache_creator->m_File)
        {
            fclose(cache_creator->m_File);
        }

        if (cache_creator->m_Filename)
        {
            dmSys::Unlink(cache_creator->m_Filename);
            free(cache_creator->m_Filename);
        }

        cache->m_CacheCreatorsPool.Push(cache_creator->m_Index);

        cache_creator->m_File = 0;
        cache_creator->m_Filename = 0;
        cache_creator->m_Index = 0xffff;
    }

    Result Add(HCache cache, HCacheCreator cache_creator, const void* content, uint32_t content_len)
    {
        assert(cache_creator->m_File && cache_creator->m_Filename);
        dmHashUpdateBuffer64(&cache_creator->m_ChecksumState, content, content_len);

        if (cache_creator->m_Error)
        {
            return RESULT_IO_ERROR;
        }

        size_t nwritten = fwrite(content, 1, content_len, cache_creator->m_File);
        if (nwritten != content_len)
        {
            dmLogError("Error writing to cache file: '%s'", cache_creator->m_Filename);
            cache_creator->m_Error = 1;
            return RESULT_IO_ERROR;
        }

        return RESULT_OK;
    }

    Result End(HCache cache, HCacheCreator cache_creator)
    {
        dmMutex::ScopedLock lock(cache->m_Mutex);

        assert(cache_creator->m_File && cache_creator->m_Filename);
        uint64_t identifier_hash = cache_creator->m_IdentifierHash;

        fclose(cache_creator->m_File);
        cache_creator->m_File = 0;

        uint64_t uri_hash = cache_creator->m_UriHash;
        Entry* entry = cache->m_CacheTable.Get(uri_hash);
        assert(entry);

        if (cache_creator->m_Error)
        {
            FreeCacheCreator(cache, cache_creator);
            cache->m_CacheTable.Erase(uri_hash);
            return RESULT_IO_ERROR;
        }

        char path[512];
        ContentFilePath(cache, identifier_hash, path, sizeof(path));
        struct stat stat_data;
        if (stat(path, &stat_data) == 0)
        {
            dmSys::Result r = dmSys::Unlink(path);
            if (r != dmSys::RESULT_OK)
            {
                dmLogError("Unable to remove cache file: %s", path);
                FreeCacheCreator(cache, cache_creator);
                cache->m_CacheTable.Erase(uri_hash);
                return RESULT_IO_ERROR;
            }
        }
        else
        {
            struct stat stat_data;
            // Modify path and remove last part of hash temporarily
            // ie, .../5d/a7b8fa56d18877 to .../5d
            char* last_slash = strrchr(path, '/');
            char save = *last_slash;
            *last_slash = '\0';
            int ret = stat(path, &stat_data);
            if (ret != 0)
            {
                dmSys::Result r = dmSys::Mkdir(path, 0755);
                if (r != dmSys::RESULT_OK)
                {
                    dmLogError("Unable to create directory '%s'", path);
                    FreeCacheCreator(cache, cache_creator);
                    cache->m_CacheTable.Erase(uri_hash);
                    return RESULT_IO_ERROR;
                }
            }
            *last_slash = save;
        }

        assert(entry->m_WriteLock);
        assert(entry->m_Info.m_IdentifierHash == identifier_hash);
        entry->m_WriteLock = 0;
        entry->m_Info.m_Checksum = dmHashFinal64(&cache_creator->m_ChecksumState);

        int ret = rename(cache_creator->m_Filename, path);
        if (ret != 0)
        {
            // TODO: strerror is not thread-safe.
            char* error_msg = strerror(errno);
            dmLogError("Unable to rename temporary cache file from '%s' to '%s'. %s (%d)", cache_creator->m_Filename, path, error_msg, errno);
            FreeCacheCreator(cache, cache_creator);
            cache->m_CacheTable.Erase(uri_hash);
            return RESULT_IO_ERROR;
        }

        FreeCacheCreator(cache, cache_creator);

        return RESULT_OK;
    }

    Result GetETag(HCache cache, const char* uri, char* tag_buffer, uint32_t tag_buffer_len)
    {
        dmMutex::ScopedLock lock(cache->m_Mutex);

        uint64_t uri_hash = dmHashString64(uri);
        Entry* entry = cache->m_CacheTable.Get(uri_hash);
        if (entry != 0)
        {
            dmStrlCpy(tag_buffer, entry->m_Info.m_ETag, tag_buffer_len);
            return RESULT_OK;
        }
        else
        {
            return RESULT_NO_ENTRY;
        }
    }

    Result GetInfo(HCache cache, const char* uri, EntryInfo* info)
    {
        dmMutex::ScopedLock lock(cache->m_Mutex);

        uint64_t uri_hash = dmHashString64(uri);
        Entry* entry = cache->m_CacheTable.Get(uri_hash);
        if (entry != 0)
        {
            memcpy(info, &entry->m_Info, sizeof(*info));
            return RESULT_OK;
        }
        else
        {
            return RESULT_NO_ENTRY;
        }
    }

    Result Get(HCache cache, const char* uri, const char* etag, FILE** file, uint64_t* checksum)
    {
        dmMutex::ScopedLock lock(cache->m_Mutex);

        HashState64 hash_state;
        dmHashInit64(&hash_state);
        dmHashUpdateBuffer64(&hash_state, uri, strlen(uri));
        dmHashUpdateBuffer64(&hash_state, etag, strlen(etag));
        uint64_t identifier_hash = dmHashFinal64(&hash_state);

        uint64_t uri_hash = dmHashString64(uri);
        Entry* entry = cache->m_CacheTable.Get(uri_hash);
        if (entry != 0 && entry->m_Info.m_IdentifierHash == identifier_hash)
        {
            if (entry->m_WriteLock)
            {
                dmLogWarning("Cache entry locked.");
                return RESULT_LOCKED;
            }

            entry->m_Info.m_LastAccessed = dmTime::GetTime();

            char path[512];
            ContentFilePath(cache, identifier_hash, path, sizeof(path));
            FILE* f = fopen(path, "rb");
            if (f)
            {
                *file = f;
                entry->m_ReadLockCount++;
                *checksum = entry->m_Info.m_Checksum;
                return RESULT_OK;
            }
            else
            {
                dmLogError("Unable to open %s", path);
                // Remove invalid cache entry
                cache->m_CacheTable.Erase(uri_hash);
                return RESULT_NO_ENTRY;
            }
        }

        return RESULT_NO_ENTRY;
    }

    Result SetVerified(HCache cache, const char* uri, bool verified)
    {
        dmMutex::ScopedLock lock(cache->m_Mutex);

        uint64_t uri_hash = dmHashString64(uri);
        Entry* entry = cache->m_CacheTable.Get(uri_hash);
        if (entry != 0)
        {
            entry->m_Info.m_Verified = verified;
            return RESULT_OK;
        }
        else
        {
            return RESULT_NO_ENTRY;
        }
    }

    Result Release(HCache cache, const char* uri, const char* etag, FILE* file)
    {
        dmMutex::ScopedLock lock(cache->m_Mutex);

        HashState64 hash_state;
        dmHashInit64(&hash_state);
        dmHashUpdateBuffer64(&hash_state, uri, strlen(uri));
        dmHashUpdateBuffer64(&hash_state, etag, strlen(etag));
        uint64_t identifier_hash = dmHashFinal64(&hash_state);

        uint64_t uri_hash = dmHashString64(uri);
        Entry* entry = cache->m_CacheTable.Get(uri_hash);
        assert(entry);
        assert(entry->m_Info.m_IdentifierHash == identifier_hash);
        assert(strcmp(uri, entry->m_Info.m_URI) == 0);
        assert(entry->m_ReadLockCount > 0);
        --entry->m_ReadLockCount;

        fclose(file);
        return RESULT_OK;
    }

    uint32_t GetEntryCount(HCache cache)
    {
        dmMutex::ScopedLock lock(cache->m_Mutex);
        return cache->m_CacheTable.Size();
    }

    struct IterateContext
    {
        IterateContext(HCache cache, void* context, void (*callback)(void* context, const EntryInfo* entry_info))
        {
            m_Cache = cache;
            m_Context = context;
            m_Callback = callback;
        }

        HCache m_Cache;
        void*  m_Context;
        void (*m_Callback)(void* context, const EntryInfo* entry_info);
    };

    static void IterateCallback(IterateContext *context, const uint64_t* key, Entry* value)
    {
        context->m_Callback(context->m_Context, &value->m_Info);
    }

    void SetConsistencyPolicy(HCache cache, ConsistencyPolicy policy)
    {
        cache->m_Policy = policy;
    }

    ConsistencyPolicy GetConsistencyPolicy(HCache cache)
    {
        return cache->m_Policy;
    }

    void Iterate(HCache cache, void* context, void (*call_back)(void* context, const EntryInfo* entry_info))
    {
        IterateContext iterate_context(cache, context, call_back);
        cache->m_CacheTable.Iterate(&IterateCallback, &iterate_context);
    }

}
