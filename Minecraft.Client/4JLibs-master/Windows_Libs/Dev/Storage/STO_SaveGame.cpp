/*
MIT License

Copyright (c) 2026 Patoke

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "STO_SaveGame.h"
#include <stdio.h>

static unsigned long s_pngCrcTable[256];
static bool          s_pngCrcTableReady = false;

static void BuildPngCrcTable()
{
    for (unsigned int n = 0; n < 256; n++)
    {
        unsigned long c = n;
        for (int k = 0; k < 8; k++)
            c = (c & 1) ? (0xEDB88320L ^ (c >> 1)) : (c >> 1);
        s_pngCrcTable[n] = c;
    }
    s_pngCrcTableReady = true;
}

static unsigned long PngCrc32(const unsigned char *buf, unsigned int len)
{
    if (!s_pngCrcTableReady) BuildPngCrcTable();
    unsigned long c = 0xFFFFFFFFL;
    for (unsigned int i = 0; i < len; i++)
        c = s_pngCrcTable[(c ^ buf[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFL;
}

static inline unsigned int WriteBE32(unsigned int v)
{
    return ((v >> 24) & 0xFF)       |
           ((v >>  8) & 0xFF00)     |
           ((v <<  8) & 0xFF0000)   |
           ((v << 24) & 0xFF000000);
}

static void GetGameHDDPath(char *outPath, int maxLen)
{
    char curDir[256];
    GetCurrentDirectoryA(sizeof(curDir), curDir);
    sprintf_s(outPath, maxLen, "%s\\Windows64\\GameHDD", curDir);
}

CSaveGame::CSaveGame()
{
    m_pSaveData = nullptr;
    m_uiSaveSize = 0;
    m_bIsSafeDisabled = false;

    ZeroMemory(m_szSaveUniqueName, sizeof(m_szSaveUniqueName));
    ZeroMemory(m_wszSaveTitle, sizeof(m_wszSaveTitle));

    m_pSaveDetails = nullptr;
    m_bHasSaveDetails = false;

    m_pbThumbnail = nullptr;
    m_dwThumbnailBytes = 0;
    m_pbDefaultThumbnail = nullptr;
    m_dwDefaultThumbnailBytes = 0;
    m_pbDefaultSaveImage = nullptr;
    m_dwDefaultSaveImageBytes = 0;

    char gameHDDPath[256];
    GetGameHDDPath(gameHDDPath, sizeof(gameHDDPath));

    char win64Path[256];
    char curDir[256];
    GetCurrentDirectoryA(sizeof(curDir), curDir);
    sprintf_s(win64Path, sizeof(win64Path), "%s\\Windows64", curDir);
    CreateDirectoryA(win64Path, 0);
    CreateDirectoryA(gameHDDPath, 0);
}

void CSaveGame::SetSaveDisabled(bool bDisable)
{
    m_bIsSafeDisabled = bDisable;
}

bool CSaveGame::GetSaveDisabled(void)
{
    return m_bIsSafeDisabled;
}

void CSaveGame::ResetSaveData()
{
    free(m_pSaveData);
    m_pSaveData = nullptr;
    m_uiSaveSize = 0;
}

C4JStorage::ESaveGameState CSaveGame::GetSavesInfo(int iPad, int (*Func)(LPVOID lpParam, SAVE_DETAILS *pSaveDetails, const bool), LPVOID lpParam,
                                                   char *pszSavePackName)
{
    WIN32_FIND_DATAA findFileData;
    WIN32_FILE_ATTRIBUTE_DATA fileInfoBuffer;

    if (!m_pSaveDetails)
    {
        m_pSaveDetails = new SAVE_DETAILS();
        memset(m_pSaveDetails, 0, sizeof(SAVE_DETAILS));
    }

    delete[] m_pSaveDetails->SaveInfoA;
    m_pSaveDetails->SaveInfoA = nullptr;
    m_pSaveDetails->iSaveC = 0;

    char gameHDDPath[256];
    GetGameHDDPath(gameHDDPath, sizeof(gameHDDPath));

    char searchPattern[280];
    sprintf_s(searchPattern, sizeof(searchPattern), "%s\\*", gameHDDPath);

    int resultCount = 0;
    HANDLE h = FindFirstFileExA(searchPattern, FindExInfoStandard, &findFileData, FindExSearchLimitToDirectories, 0, 0);
    if (h == INVALID_HANDLE_VALUE)
    {
        DWORD error = GetLastError();
        printf("Error finding save dirs: 0x%08x\n", error);
    }
    else
    {
        do
        {
            if ((findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
                strcmp(findFileData.cFileName, ".") != 0 &&
                strcmp(findFileData.cFileName, "..") != 0)
            {
                char saveFilePath[512];
                sprintf_s(saveFilePath, sizeof(saveFilePath), "%s\\%s\\saveData.ms", gameHDDPath, findFileData.cFileName);
                if (GetFileAttributesA(saveFilePath) != INVALID_FILE_ATTRIBUTES)
                {
                    resultCount++;
                }
            }
        } while (FindNextFileA(h, &findFileData));
        FindClose(h);
    }

    if (resultCount > 0)
    {
        m_pSaveDetails->SaveInfoA = new SAVE_INFO[resultCount];
        memset(m_pSaveDetails->SaveInfoA, 0, sizeof(SAVE_INFO) * resultCount);

        m_pSaveDetails->iSaveC = 0;
        int i = 0;
        HANDLE fi = FindFirstFileExA(searchPattern, FindExInfoStandard, &findFileData, FindExSearchLimitToDirectories, 0, 0);
        if (fi != INVALID_HANDLE_VALUE)
        {
            do
            {
                if ((findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
                    strcmp(findFileData.cFileName, ".") != 0 &&
                    strcmp(findFileData.cFileName, "..") != 0)
                {
                    char saveFilePath[512];
                    sprintf_s(saveFilePath, sizeof(saveFilePath), "%s\\%s\\saveData.ms", gameHDDPath, findFileData.cFileName);

                    if (GetFileAttributesA(saveFilePath) == INVALID_FILE_ATTRIBUTES)
                        continue;

                    strcpy_s(m_pSaveDetails->SaveInfoA[i].UTF8SaveFilename, findFileData.cFileName);

                    char saveDirPath[512];
                    sprintf_s(saveDirPath, sizeof(saveDirPath), "%s\\%s", gameHDDPath, findFileData.cFileName);

                    char titleBuf[MAX_DISPLAYNAME_LENGTH];
                    if (LoadTitleFromFile(saveDirPath, titleBuf, sizeof(titleBuf)))
                    {
                        strcpy_s(m_pSaveDetails->SaveInfoA[i].UTF8SaveTitle, titleBuf);
                    }
                    else
                    {
                        // fallback: use the folder name as the display title
                        strcpy_s(m_pSaveDetails->SaveInfoA[i].UTF8SaveTitle, findFileData.cFileName);
                    }

                    GetFileAttributesExA(saveFilePath, GetFileExInfoStandard, &fileInfoBuffer);
                    m_pSaveDetails->SaveInfoA[i].metaData.dataSize = fileInfoBuffer.nFileSizeLow;

                    char thumbFilePath[512];
                    sprintf_s(thumbFilePath, sizeof(thumbFilePath), "%s\\%s\\saveThumbnail.png", gameHDDPath, findFileData.cFileName);
                    WIN32_FILE_ATTRIBUTE_DATA thumbInfo;
                    if (GetFileAttributesExA(thumbFilePath, GetFileExInfoStandard, &thumbInfo))
                    {
                        m_pSaveDetails->SaveInfoA[i].metaData.thumbnailSize = thumbInfo.nFileSizeLow;
                    }

                    FILETIME ft = fileInfoBuffer.ftLastWriteTime;
                    ULARGE_INTEGER ull;
                    ull.LowPart = ft.dwLowDateTime;
                    ull.HighPart = ft.dwHighDateTime;

                    m_pSaveDetails->SaveInfoA[i].metaData.modifiedTime = (time_t)((ull.QuadPart - 116444736000000000ULL) / 10000000ULL);

                    i++;
                    m_pSaveDetails->iSaveC++;
                }
            } while (FindNextFileA(fi, &findFileData));
            FindClose(fi);
        }
    }

    m_bHasSaveDetails = true;
    if (Func)
    {
        Func(lpParam, m_pSaveDetails, true);
    }

    return C4JStorage::ESaveGame_Idle;
}

PSAVE_DETAILS CSaveGame::ReturnSavesInfo()
{
    if (m_bHasSaveDetails)
        return m_pSaveDetails;
    else
        return nullptr;
}

void CSaveGame::ClearSavesInfo()
{
    m_bHasSaveDetails = false;
    if (m_pSaveDetails)
    {
        if (m_pSaveDetails->SaveInfoA)
        {
            delete[] m_pSaveDetails->SaveInfoA;
            m_pSaveDetails->SaveInfoA = nullptr;
            m_pSaveDetails->iSaveC = 0;
        }
        delete m_pSaveDetails;
        m_pSaveDetails = 0;
    }
}

C4JStorage::ESaveGameState CSaveGame::LoadSaveDataThumbnail(PSAVE_INFO pSaveInfo,
                                                            int (*Func)(LPVOID lpParam, PBYTE pbThumbnail, DWORD dwThumbnailBytes), LPVOID lpParam)
{
    PBYTE pbThumbnail = nullptr;
    DWORD dwThumbnailBytes = 0;

    char gameHDDPath[256];
    GetGameHDDPath(gameHDDPath, sizeof(gameHDDPath));

    char thumbPath[512];
    sprintf_s(thumbPath, sizeof(thumbPath), "%s\\%s\\saveThumbnail.png", gameHDDPath, pSaveInfo->UTF8SaveFilename);

    HANDLE h = CreateFileA(thumbPath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (h != INVALID_HANDLE_VALUE)
    {
        DWORD fileSize = GetFileSize(h, NULL);
        if (fileSize != 0 && fileSize != INVALID_FILE_SIZE)
        {
            pbThumbnail = (PBYTE)malloc(fileSize);
            if (pbThumbnail)
            {
                DWORD bytesRead = 0;
                if (ReadFile(h, pbThumbnail, fileSize, &bytesRead, 0) && bytesRead == fileSize)
                {
                    dwThumbnailBytes = fileSize;
                }
                else
                {
                    free(pbThumbnail);
                    pbThumbnail = nullptr;
                }
            }
        }
        CloseHandle(h);
    }

    Func(lpParam, pbThumbnail, dwThumbnailBytes);

    if (pbThumbnail)
    {
        free(pbThumbnail);
    }

    return C4JStorage::ESaveGame_GetSaveThumbnail;
}

C4JStorage::ESaveGameState CSaveGame::LoadSaveData(PSAVE_INFO pSaveInfo, int (*Func)(LPVOID lpParam, const bool, const bool), LPVOID lpParam)
{
    SetSaveUniqueFilename(pSaveInfo->UTF8SaveFilename);

    if (m_pSaveData)
    {
        free(m_pSaveData);
        m_pSaveData = nullptr;
    }

    char gameHDDPath[256];
    GetGameHDDPath(gameHDDPath, sizeof(gameHDDPath));

    char saveDirPath[512];
    sprintf_s(saveDirPath, sizeof(saveDirPath), "%s\\%s", gameHDDPath, m_szSaveUniqueName);

    char titleBuf[MAX_DISPLAYNAME_LENGTH];
    if (LoadTitleFromFile(saveDirPath, titleBuf, sizeof(titleBuf)))
    {
        MultiByteToWideChar(CP_UTF8, 0, titleBuf, -1, m_wszSaveTitle, MAX_DISPLAYNAME_LENGTH);
    }

    char fileName[512];
    sprintf_s(fileName, sizeof(fileName), "%s\\saveData.ms", saveDirPath);

    WIN32_FILE_ATTRIBUTE_DATA fileInfo;
    if (!GetFileAttributesExA(fileName, GetFileExInfoStandard, &fileInfo))
    {
        if (Func) Func(lpParam, 0, false);
        return C4JStorage::ESaveGame_Idle;
    }

    m_uiSaveSize = fileInfo.nFileSizeLow;
    m_pSaveData = malloc(m_uiSaveSize);

    HANDLE h = CreateFileA(fileName, GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);

    bool success = false;
    if (h != INVALID_HANDLE_VALUE)
    {
        DWORD bytesRead = 0;
        BOOL res = ReadFile(h, m_pSaveData, m_uiSaveSize, &bytesRead, 0);
        _ASSERT(res && bytesRead == m_uiSaveSize);
        CloseHandle(h);
        success = (res && bytesRead == m_uiSaveSize);
    }

    if (!success && m_pSaveData)
    {
        free(m_pSaveData);
        m_pSaveData = nullptr;
        m_uiSaveSize = 0;
    }

    if (Func)
    {
        Func(lpParam, 0, success);
    }

    return C4JStorage::ESaveGame_Idle;
}

unsigned int CSaveGame::GetSaveSize()
{
    return m_uiSaveSize;
}

void CSaveGame::GetSaveData(void *pvData, unsigned int *puiBytes)
{
    if (pvData)
    {
        memmove(pvData, m_pSaveData, m_uiSaveSize);
        *puiBytes = m_uiSaveSize;
    }
    else
    {
        *puiBytes = 0;
    }
}

bool CSaveGame::GetSaveUniqueNumber(INT *piVal)
{
    *piVal = 0;

    if (m_szSaveUniqueName[0] != '\0')
    {
        *piVal = atoi(m_szSaveUniqueName);
        return true;
    }
    return false;
}

bool CSaveGame::GetSaveUniqueFilename(char *pszName)
{
    if (m_szSaveUniqueName[0] != '\0')
    {
        strcpy_s(pszName, MAX_SAVEFILENAME_LENGTH, m_szSaveUniqueName);
        return true;
    }
    return false;
}

void CSaveGame::SetSaveTitle(LPCWSTR pwchDefaultSaveName)
{
    if (m_szSaveUniqueName[0] == '\0')
    {
        CreateSaveUniqueName();
    }
    wcscpy_s(m_wszSaveTitle, MAX_DISPLAYNAME_LENGTH, pwchDefaultSaveName);
}

LPCWSTR CSaveGame::GetSaveTitle()
{
    return m_wszSaveTitle;
}

PVOID CSaveGame::AllocateSaveData(unsigned int uiBytes)
{
    free(m_pSaveData);

    m_pSaveData = malloc(uiBytes);
    if (m_pSaveData)
    {
        m_uiSaveSize = uiBytes;
    }

    return m_pSaveData;
}

// https://github.com/LCEMP/LCEMP
void CSaveGame::SetSaveImages(PBYTE pbThumbnail, DWORD dwThumbnailBytes, PBYTE pbImage, DWORD dwImageBytes, PBYTE pbTextData, DWORD dwTextDataBytes)
{
    if (m_pbThumbnail)
    {
        free(m_pbThumbnail);
        m_pbThumbnail = nullptr;
        m_dwThumbnailBytes = 0;
    }


    if (pbThumbnail && dwThumbnailBytes > 0)
    {
        const DWORD kInsertOffset = 33; // end of PNG sig + IHDR

        if (pbTextData && dwTextDataBytes > 0 && dwThumbnailBytes > kInsertOffset)
        {
            const DWORD chunkOverhead = 4 + 4 + 4;
            const DWORD chunkTotal    = chunkOverhead + dwTextDataBytes;
            const DWORD newSize       = dwThumbnailBytes + chunkTotal;

            m_pbThumbnail = (PBYTE)malloc(newSize);
            if (m_pbThumbnail)
            {
                memcpy(m_pbThumbnail, pbThumbnail, kInsertOffset);

                PBYTE p = m_pbThumbnail + kInsertOffset;

                *(unsigned int *)p = WriteBE32(dwTextDataBytes);
                p += 4;

                p[0] = 't'; p[1] = 'E'; p[2] = 'X'; p[3] = 't';
                p += 4;

                memcpy(p, pbTextData, dwTextDataBytes);
                p += dwTextDataBytes;

                unsigned long crc = PngCrc32(m_pbThumbnail + kInsertOffset + 4,
                                             4 + dwTextDataBytes);
                *(unsigned int *)p = WriteBE32((unsigned int)crc);

                memcpy(m_pbThumbnail + kInsertOffset + chunkTotal,
                       pbThumbnail  + kInsertOffset,
                       dwThumbnailBytes - kInsertOffset);

                m_dwThumbnailBytes = newSize;
            }
        }
        else
        {
            m_pbThumbnail = (PBYTE)malloc(dwThumbnailBytes);
            if (m_pbThumbnail)
            {
                memcpy(m_pbThumbnail, pbThumbnail, dwThumbnailBytes);
                m_dwThumbnailBytes = dwThumbnailBytes;
            }
        }
    }
}

// https://github.com/LCEMP/LCEMP
C4JStorage::ESaveGameState CSaveGame::SaveSaveData(int (*Func)(LPVOID, const bool), LPVOID lpParam)
{
    if (!m_pSaveData || m_uiSaveSize == 0)
    {
        if (Func) Func(lpParam, false);
        return C4JStorage::ESaveGame_Idle;
    }

    char gameHDDPath[256];
    GetGameHDDPath(gameHDDPath, sizeof(gameHDDPath));

    char saveDirPath[512];
    sprintf_s(saveDirPath, sizeof(saveDirPath), "%s\\%s", gameHDDPath, m_szSaveUniqueName);
    CreateDirectoryA(saveDirPath, 0);

    char saveFilePath[512];
    sprintf_s(saveFilePath, sizeof(saveFilePath), "%s\\saveData.ms", saveDirPath);

    HANDLE h = CreateFileA(saveFilePath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE_VALUE)
    {
        if (Func) Func(lpParam, false);
        return C4JStorage::ESaveGame_Idle;
    }

    DWORD bytesWritten = 0;
    BOOL res = WriteFile(h, m_pSaveData, m_uiSaveSize, &bytesWritten, 0);
    CloseHandle(h);


    SaveTitleFile(saveDirPath);


    if (m_pbThumbnail && m_dwThumbnailBytes > 0)
    {
        char thumbPath[512];
        sprintf_s(thumbPath, sizeof(thumbPath), "%s\\saveThumbnail.png", saveDirPath);

        HANDLE hThumb = CreateFileA(thumbPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
        if (hThumb != INVALID_HANDLE_VALUE)
        {
            DWORD thumbWritten = 0;
            WriteFile(hThumb, m_pbThumbnail, m_dwThumbnailBytes, &thumbWritten, 0);
            CloseHandle(hThumb);
        }


        free(m_pbThumbnail);
        m_pbThumbnail = nullptr;
        m_dwThumbnailBytes = 0;
    }

    bool success = (res && bytesWritten == m_uiSaveSize);
    if (Func) Func(lpParam, success);

    return C4JStorage::ESaveGame_Idle;
}

C4JStorage::ESaveGameState CSaveGame::DeleteSaveData(PSAVE_INFO pSaveInfo, int (*Func)(LPVOID lpParam, const bool), LPVOID lpParam)
{
    char gameHDDPath[256];
    GetGameHDDPath(gameHDDPath, sizeof(gameHDDPath));

    char saveDirPath[512];
    sprintf_s(saveDirPath, sizeof(saveDirPath), "%s\\%s", gameHDDPath, pSaveInfo->UTF8SaveFilename);

    char searchPattern[512];
    sprintf_s(searchPattern, sizeof(searchPattern), "%s\\*", saveDirPath);

    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA(searchPattern, &findData);
    if (hFind != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            {
                char filePath[512];
                sprintf_s(filePath, sizeof(filePath), "%s\\%s", saveDirPath, findData.cFileName);
                DeleteFileA(filePath);
            }
        } while (FindNextFileA(hFind, &findData));
        FindClose(hFind);
    }

    RemoveDirectoryA(saveDirPath);

    bool success = (GetFileAttributesA(saveDirPath) == INVALID_FILE_ATTRIBUTES);

    if (Func) Func(lpParam, success);

    return C4JStorage::ESaveGame_Idle;
}

C4JStorage::ESaveGameState CSaveGame::DoesSaveExist(bool *pbExists)
{
    if (m_szSaveUniqueName[0] == '\0')
    {
        *pbExists = false;
        return C4JStorage::ESaveGame_Idle;
    }

    char gameHDDPath[256];
    GetGameHDDPath(gameHDDPath, sizeof(gameHDDPath));

    char saveFilePath[512];
    sprintf_s(saveFilePath, sizeof(saveFilePath), "%s\\%s\\saveData.ms", gameHDDPath, m_szSaveUniqueName);

    *pbExists = (GetFileAttributesA(saveFilePath) != INVALID_FILE_ATTRIBUTES);
    return C4JStorage::ESaveGame_Idle;
}

void CSaveGame::CopySaveDataToNewSave(PBYTE pbThumbnail, DWORD cbThumbnail, WCHAR *wchNewName, int (*Func)(LPVOID lpParam, bool), LPVOID lpParam)
{
    char oldUniqueName[32];
    strcpy_s(oldUniqueName, m_szSaveUniqueName);

    CreateSaveUniqueName();

    char gameHDDPath[256];
    GetGameHDDPath(gameHDDPath, sizeof(gameHDDPath));

    char newSaveDirPath[512];
    sprintf_s(newSaveDirPath, sizeof(newSaveDirPath), "%s\\%s", gameHDDPath, m_szSaveUniqueName);
    CreateDirectoryA(newSaveDirPath, 0);

    char oldSaveFile[512], newSaveFile[512];
    sprintf_s(oldSaveFile, sizeof(oldSaveFile), "%s\\%s\\saveData.ms", gameHDDPath, oldUniqueName);
    sprintf_s(newSaveFile, sizeof(newSaveFile), "%s\\saveData.ms", newSaveDirPath);
    CopyFileA(oldSaveFile, newSaveFile, FALSE);

    char oldThumbFile[512], newThumbFile[512];
    sprintf_s(oldThumbFile, sizeof(oldThumbFile), "%s\\%s\\saveThumbnail.png", gameHDDPath, oldUniqueName);
    sprintf_s(newThumbFile, sizeof(newThumbFile), "%s\\saveThumbnail.png", newSaveDirPath);
    CopyFileA(oldThumbFile, newThumbFile, FALSE);

    if (pbThumbnail && cbThumbnail > 0)
    {
        HANDLE hThumb = CreateFileA(newThumbFile, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
        if (hThumb != INVALID_HANDLE_VALUE)
        {
            DWORD thumbWritten = 0;
            WriteFile(hThumb, pbThumbnail, cbThumbnail, &thumbWritten, 0);
            CloseHandle(hThumb);
        }
    }

    if (wchNewName)
    {
        wcscpy_s(m_wszSaveTitle, MAX_DISPLAYNAME_LENGTH, wchNewName);
    }
    SaveTitleFile(newSaveDirPath);

    bool success = (GetFileAttributesA(newSaveFile) != INVALID_FILE_ATTRIBUTES);
    if (Func) Func(lpParam, success);
}

void CSaveGame::SetSaveUniqueFilename(char *szFilename)
{
    strcpy_s(m_szSaveUniqueName, szFilename);
}

void CSaveGame::CreateSaveUniqueName(void)
{
    _SYSTEMTIME UTCSysTime;
    GetSystemTime(&UTCSysTime);

    sprintf_s(m_szSaveUniqueName, sizeof(m_szSaveUniqueName), "%4d%02d%02d%02d%02d%02d", UTCSysTime.wYear, UTCSysTime.wMonth, UTCSysTime.wDay,
              UTCSysTime.wHour, UTCSysTime.wMinute, UTCSysTime.wSecond);
}

void CSaveGame::SaveTitleFile(const char *saveDirPath)
{
    if (m_wszSaveTitle[0] == L'\0')
        return;

    char titleFilePath[512];
    sprintf_s(titleFilePath, sizeof(titleFilePath), "%s\\saveTitle.txt", saveDirPath);

    char utf8Title[MAX_DISPLAYNAME_LENGTH * 3];
    int len = WideCharToMultiByte(CP_UTF8, 0, m_wszSaveTitle, -1, utf8Title, sizeof(utf8Title), NULL, NULL);

    if (len > 0)
    {
        HANDLE h = CreateFileA(titleFilePath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
        if (h != INVALID_HANDLE_VALUE)
        {
            DWORD bytesWritten = 0;
            WriteFile(h, utf8Title, len - 1, &bytesWritten, 0); // don't write null terminator
            CloseHandle(h);
        }
    }
}

bool CSaveGame::LoadTitleFromFile(const char *saveDirPath, char *outUTF8Title, int maxLen)
{
    char titleFilePath[512];
    sprintf_s(titleFilePath, sizeof(titleFilePath), "%s\\saveTitle.txt", saveDirPath);

    HANDLE h = CreateFileA(titleFilePath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE_VALUE)
        return false;

    DWORD fileSize = GetFileSize(h, NULL);
    if (fileSize == 0 || fileSize == INVALID_FILE_SIZE || fileSize >= (DWORD)maxLen)
    {
        CloseHandle(h);
        return false;
    }

    DWORD bytesRead = 0;
    ReadFile(h, outUTF8Title, fileSize, &bytesRead, 0);
    CloseHandle(h);

    if (bytesRead > 0)
    {
        outUTF8Title[bytesRead] = '\0';
        return true;
    }
    return false;
}

void CSaveGame::SetDefaultImages(PBYTE pbOptionsImage, DWORD dwOptionsImageBytes, PBYTE pbSaveImage, DWORD dwSaveImageBytes, PBYTE pbSaveThumbnail, DWORD dwSaveThumbnailBytes)
{
    if (m_pbDefaultSaveImage)
    {
        free(m_pbDefaultSaveImage);
        m_pbDefaultSaveImage = nullptr;
        m_dwDefaultSaveImageBytes = 0;
    }
    if (pbSaveImage && dwSaveImageBytes > 0)
    {
        m_pbDefaultSaveImage = (PBYTE)malloc(dwSaveImageBytes);
        if (m_pbDefaultSaveImage)
        {
            memcpy(m_pbDefaultSaveImage, pbSaveImage, dwSaveImageBytes);
            m_dwDefaultSaveImageBytes = dwSaveImageBytes;
        }
    }

    if (m_pbDefaultThumbnail)
    {
        free(m_pbDefaultThumbnail);
        m_pbDefaultThumbnail = nullptr;
        m_dwDefaultThumbnailBytes = 0;
    }
    if (pbSaveThumbnail && dwSaveThumbnailBytes > 0)
    {
        m_pbDefaultThumbnail = (PBYTE)malloc(dwSaveThumbnailBytes);
        if (m_pbDefaultThumbnail)
        {
            memcpy(m_pbDefaultThumbnail, pbSaveThumbnail, dwSaveThumbnailBytes);
            m_dwDefaultThumbnailBytes = dwSaveThumbnailBytes;
        }
    }
}

void CSaveGame::GetDefaultSaveImage(PBYTE *ppbSaveImage, DWORD *pdwSaveImageBytes)
{
    if (ppbSaveImage) *ppbSaveImage = m_pbDefaultSaveImage;
    if (pdwSaveImageBytes) *pdwSaveImageBytes = m_dwDefaultSaveImageBytes;
}

void CSaveGame::GetDefaultSaveThumbnail(PBYTE *ppbSaveThumbnail, DWORD *pdwSaveThumbnailBytes)
{
    if (ppbSaveThumbnail) *ppbSaveThumbnail = m_pbDefaultThumbnail;
    if (pdwSaveThumbnailBytes) *pdwSaveThumbnailBytes = m_dwDefaultThumbnailBytes;
}

C4JStorage::ESaveGameState CSaveGame::RenameSaveData(int iRenameIndex, uint16_t *pui16NewName, int (*Func)(LPVOID lpParam, const bool), LPVOID lpParam)
{
    bool bSuccess = false;

    if (m_pSaveDetails && iRenameIndex >= 0 && iRenameIndex < m_pSaveDetails->iSaveC && pui16NewName)
    {
        char gameHDDPath[256];
        GetGameHDDPath(gameHDDPath, sizeof(gameHDDPath));

        char saveDirPath[512];
        sprintf_s(saveDirPath, sizeof(saveDirPath), "%s\\%s", gameHDDPath, m_pSaveDetails->SaveInfoA[iRenameIndex].UTF8SaveFilename);

        wchar_t newTitle[MAX_DISPLAYNAME_LENGTH];
        int i = 0;
        while (i < MAX_DISPLAYNAME_LENGTH - 1 && pui16NewName[i] != 0)
        {
            newTitle[i] = (wchar_t)pui16NewName[i];
            i++;
        }
        newTitle[i] = L'\0';

        char titleFilePath[512];
        sprintf_s(titleFilePath, sizeof(titleFilePath), "%s\\saveTitle.txt", saveDirPath);

        char utf8Title[MAX_DISPLAYNAME_LENGTH * 3];
        int len = WideCharToMultiByte(CP_UTF8, 0, newTitle, -1, utf8Title, sizeof(utf8Title), NULL, NULL);

        if (len > 0)
        {
            HANDLE h = CreateFileA(titleFilePath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
            if (h != INVALID_HANDLE_VALUE)
            {
                DWORD bytesWritten = 0;
                WriteFile(h, utf8Title, len - 1, &bytesWritten, 0);
                CloseHandle(h);

                WideCharToMultiByte(CP_UTF8, 0, newTitle, -1, m_pSaveDetails->SaveInfoA[iRenameIndex].UTF8SaveTitle, MAX_DISPLAYNAME_LENGTH, NULL, NULL);
                bSuccess = true;
            }
        }
    }

    if (Func) Func(lpParam, bSuccess);
    return C4JStorage::ESaveGame_Rename;
}
