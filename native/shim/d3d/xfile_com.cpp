// The two .x enumeration APIs the client uses, over the tree in xfile_parse.
//
//   ID3DXFile / ID3DXFileEnumObject / ID3DXFileData   — DxFrameMesh
//   IDirectXFile / ...EnumObject / ...Data            — DxBoneCollector
//
// Both are read-only views of the same parsed file, so one tree backs both and
// the two facades differ only in method names and string-length conventions.
// Saving is not implemented: nothing in the client writes .x files.

#include "windows.h"
#include <d3d9.h>
#include <d3dx9.h>
#include <dxfile.h>

#include "xfile_parse.h"


#include <android/log.h>
#include <string.h>
#include <string>
#include <vector>

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "RanXFile", __VA_ARGS__)

// Reads a whole file through the path resolver.
extern "C" FILE *ran_fopen(const char *path, const char *mode);

namespace {

bool readWhole(const char *path, std::vector<BYTE> &out) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return false; }
    out.resize((size_t)n);
    size_t got = fread(&out[0], 1, (size_t)n, f);
    fclose(f);
    out.resize(got);
    return got > 0;
}

// A file shared by every object handed out from it, so the tree outlives the
// enumerator the caller released first.
struct SharedFile {
    XFile *file;
    LONG   refs;
    explicit SharedFile(XFile *f) : file(f), refs(1) {}
    void addRef() { ++refs; }
    void release() { if (--refs <= 0) { delete file; delete this; } }
};

// ------------------------------------------------------------- D3DX facade
class RanD3DXFileData : public ID3DXFileData {
public:
    LONG m_ref;
    SharedFile *m_shared;
    XNode *m_node;

    RanD3DXFileData(SharedFile *s, XNode *n) : m_ref(1), m_shared(s), m_node(n) { m_shared->addRef(); }
    ~RanD3DXFileData() { m_shared->release(); }

    HRESULT __stdcall QueryInterface(REFIID, void **ppv) { *ppv = this; AddRef(); return S_OK; }
    ULONG   __stdcall AddRef() { return (ULONG)++m_ref; }
    ULONG   __stdcall Release() { LONG r = --m_ref; if (r <= 0) { delete this; return 0; } return (ULONG)r; }

    HRESULT __stdcall GetEnum(ID3DXFileEnumObject **ppEnum) { if (ppEnum) *ppEnum = NULL; return E_NOTIMPL; }
    HRESULT __stdcall GetName(LPSTR pstrName, SIZE_T *pCount) {
        if (!pCount) return D3DERR_INVALIDCALL;
        // D3DX's convention: the count includes the terminator.
        SIZE_T need = m_node->name.size() + 1;
        if (!pstrName) { *pCount = need; return D3D_OK; }
        if (*pCount < need) return D3DXFERR_BADVALUE;
        memcpy(pstrName, m_node->name.c_str(), need);
        *pCount = need;
        return D3D_OK;
    }
    HRESULT __stdcall GetId(LPGUID pId) { if (pId) memset(pId, 0, sizeof(*pId)); return D3D_OK; }
    HRESULT __stdcall Lock(SIZE_T *pSize, LPCVOID *ppData) {
        if (!pSize || !ppData) return D3DERR_INVALIDCALL;
        *pSize = m_node->data.size();
        *ppData = m_node->data.empty() ? NULL : &m_node->data[0];
        return D3D_OK;
    }
    HRESULT __stdcall Unlock() { return D3D_OK; }
    HRESULT __stdcall GetType(GUID *pType) { if (pType) *pType = m_node->type; return D3D_OK; }
    BOOL    __stdcall IsReference() { return m_node->typeName == "__reference" ? TRUE : FALSE; }
    HRESULT __stdcall GetChildren(SIZE_T *pChildren) {
        if (!pChildren) return D3DERR_INVALIDCALL;
        *pChildren = m_node->children.size();
        return D3D_OK;
    }
    HRESULT __stdcall GetChild(SIZE_T id, ID3DXFileData **ppChild) {
        if (!ppChild) return D3DERR_INVALIDCALL;
        if (id >= m_node->children.size()) return D3DXFERR_BADVALUE;
        *ppChild = new RanD3DXFileData(m_shared, m_node->children[id]);
        return D3D_OK;
    }
};

class RanD3DXFileEnum : public ID3DXFileEnumObject {
public:
    LONG m_ref;
    SharedFile *m_shared;

    explicit RanD3DXFileEnum(SharedFile *s) : m_ref(1), m_shared(s) {}
    ~RanD3DXFileEnum() { m_shared->release(); }

    HRESULT __stdcall QueryInterface(REFIID, void **ppv) { *ppv = this; AddRef(); return S_OK; }
    ULONG   __stdcall AddRef() { return (ULONG)++m_ref; }
    ULONG   __stdcall Release() { LONG r = --m_ref; if (r <= 0) { delete this; return 0; } return (ULONG)r; }

    HRESULT __stdcall GetFile(ID3DXFile **ppFile) { if (ppFile) *ppFile = NULL; return E_NOTIMPL; }
    HRESULT __stdcall GetChildren(SIZE_T *pChildren) {
        if (!pChildren) return D3DERR_INVALIDCALL;
        *pChildren = m_shared->file->roots.size();
        return D3D_OK;
    }
    HRESULT __stdcall GetChild(SIZE_T id, ID3DXFileData **ppChild) {
        if (!ppChild) return D3DERR_INVALIDCALL;
        if (id >= m_shared->file->roots.size()) return D3DXFERR_BADVALUE;
        *ppChild = new RanD3DXFileData(m_shared, m_shared->file->roots[id]);
        return D3D_OK;
    }
    HRESULT __stdcall GetDataObjectById(REFGUID, ID3DXFileData **ppData) {
        if (ppData) *ppData = NULL;
        return D3DXFERR_NOTFOUND;
    }
    HRESULT __stdcall GetDataObjectByName(LPCSTR name, ID3DXFileData **ppData) {
        if (!ppData) return D3DERR_INVALIDCALL;
        *ppData = NULL;
        if (!name) return D3DXFERR_NOTFOUND;
        for (size_t i = 0; i < m_shared->file->roots.size(); ++i) {
            if (m_shared->file->roots[i]->name == name) {
                *ppData = new RanD3DXFileData(m_shared, m_shared->file->roots[i]);
                return D3D_OK;
            }
        }
        return D3DXFERR_NOTFOUND;
    }
};

class RanD3DXFile : public ID3DXFile {
public:
    LONG m_ref;
    RanD3DXFile() : m_ref(1) {}

    HRESULT __stdcall QueryInterface(REFIID, void **ppv) { *ppv = this; AddRef(); return S_OK; }
    ULONG   __stdcall AddRef() { return (ULONG)++m_ref; }
    ULONG   __stdcall Release() { LONG r = --m_ref; if (r <= 0) { delete this; return 0; } return (ULONG)r; }

    HRESULT __stdcall CreateEnumObject(LPCVOID pSource, D3DXF_FILELOADOPTIONS options,
                                       ID3DXFileEnumObject **ppEnumObj) {
        if (!ppEnumObj) return D3DERR_INVALIDCALL;
        *ppEnumObj = NULL;

        std::vector<BYTE> bytes;
        const void *data = NULL;
        size_t size = 0;

        if (options == D3DXF_FILELOAD_FROMFILE) {
            if (!pSource || !readWhole((const char *)pSource, bytes)) return D3DXFERR_FILENOTFOUND;
            data = &bytes[0];
            size = bytes.size();
        } else if (options == D3DXF_FILELOAD_FROMMEMORY) {
            const D3DXF_FILELOADMEMORY *mem = (const D3DXF_FILELOADMEMORY *)pSource;
            if (!mem || !mem->lpMemory) return D3DERR_INVALIDCALL;
            data = mem->lpMemory;
            size = mem->dSize;
        } else {
            return D3DERR_INVALIDCALL;
        }

        XFile *parsed = XFile_Parse(data, size);
        if (!parsed) return D3DXFERR_BADFILETYPE;

        *ppEnumObj = new RanD3DXFileEnum(new SharedFile(parsed));
        return D3D_OK;
    }
    HRESULT __stdcall CreateSaveObject(LPCVOID, D3DXF_FILESAVEOPTIONS, D3DXF_FILEFORMAT,
                                       ID3DXFileSaveObject **ppSaveObj) {
        if (ppSaveObj) *ppSaveObj = NULL;
        return E_NOTIMPL;                      // nothing in the client writes .x
    }
    // Templates are built in; the standard D3DRM set is all the client registers.
    HRESULT __stdcall RegisterTemplates(LPCVOID, SIZE_T) { return D3D_OK; }
    HRESULT __stdcall RegisterEnumTemplates(ID3DXFileEnumObject *) { return D3D_OK; }
};

// ----------------------------------------------------------- legacy facade
class RanXFileData : public IDirectXFileData {
public:
    LONG m_ref;
    SharedFile *m_shared;
    XNode *m_node;
    size_t m_nextChild;
    GUID m_type;

    RanXFileData(SharedFile *s, XNode *n)
        : m_ref(1), m_shared(s), m_node(n), m_nextChild(0), m_type(n->type) { m_shared->addRef(); }
    ~RanXFileData() { m_shared->release(); }

    HRESULT __stdcall QueryInterface(REFIID, void **ppv) { *ppv = this; AddRef(); return S_OK; }
    ULONG   __stdcall AddRef() { return (ULONG)++m_ref; }
    ULONG   __stdcall Release() { LONG r = --m_ref; if (r <= 0) { delete this; return 0; } return (ULONG)r; }

    HRESULT __stdcall GetName(LPSTR pstrName, LPDWORD pdwBufLen) {
        if (!pdwBufLen) return DXFILEERR_BADVALUE;
        DWORD need = (DWORD)m_node->name.size() + 1;
        if (!pstrName) { *pdwBufLen = need; return DXFILE_OK; }
        if (*pdwBufLen < need) return DXFILEERR_BADVALUE;
        memcpy(pstrName, m_node->name.c_str(), need);
        *pdwBufLen = need;
        return DXFILE_OK;
    }
    HRESULT __stdcall GetId(LPGUID pGuid) { if (pGuid) memset(pGuid, 0, sizeof(*pGuid)); return DXFILE_OK; }

    HRESULT __stdcall GetData(LPCSTR, DWORD *pcbSize, void **ppvData) {
        if (!pcbSize || !ppvData) return DXFILEERR_BADVALUE;
        *pcbSize = (DWORD)m_node->data.size();
        *ppvData = m_node->data.empty() ? NULL : (void *)&m_node->data[0];
        return DXFILE_OK;
    }
    HRESULT __stdcall GetType(const GUID **ppguid) {
        if (!ppguid) return DXFILEERR_BADVALUE;
        *ppguid = &m_type;
        return DXFILE_OK;
    }
    HRESULT __stdcall GetNextObject(LPDIRECTXFILEOBJECT *ppChildObj) {
        if (!ppChildObj) return DXFILEERR_BADVALUE;
        if (m_nextChild >= m_node->children.size()) return DXFILEERR_NOMOREOBJECTS;
        *ppChildObj = (LPDIRECTXFILEOBJECT) new RanXFileData(m_shared, m_node->children[m_nextChild++]);
        return DXFILE_OK;
    }
    HRESULT __stdcall AddDataObject(LPDIRECTXFILEDATA) { return E_NOTIMPL; }
    HRESULT __stdcall AddDataReference(LPCSTR, const GUID *) { return E_NOTIMPL; }
    HRESULT __stdcall AddBinaryObject(LPCSTR, const GUID *, LPCSTR, LPVOID, DWORD) { return E_NOTIMPL; }
};

class RanXFileEnum : public IDirectXFileEnumObject {
public:
    LONG m_ref;
    SharedFile *m_shared;
    size_t m_next;

    explicit RanXFileEnum(SharedFile *s) : m_ref(1), m_shared(s), m_next(0) {}
    ~RanXFileEnum() { m_shared->release(); }

    HRESULT __stdcall QueryInterface(REFIID, void **ppv) { *ppv = this; AddRef(); return S_OK; }
    ULONG   __stdcall AddRef() { return (ULONG)++m_ref; }
    ULONG   __stdcall Release() { LONG r = --m_ref; if (r <= 0) { delete this; return 0; } return (ULONG)r; }

    HRESULT __stdcall GetNextDataObject(LPDIRECTXFILEDATA *ppData) {
        if (!ppData) return DXFILEERR_BADVALUE;
        if (m_next >= m_shared->file->roots.size()) return DXFILEERR_NOMOREOBJECTS;
        *ppData = new RanXFileData(m_shared, m_shared->file->roots[m_next++]);
        return DXFILE_OK;
    }
    HRESULT __stdcall GetDataObjectById(REFGUID, LPDIRECTXFILEDATA *ppData) {
        if (ppData) *ppData = NULL;
        return DXFILEERR_NOTFOUND;
    }
    HRESULT __stdcall GetDataObjectByName(LPCSTR name, LPDIRECTXFILEDATA *ppData) {
        if (!ppData) return DXFILEERR_BADVALUE;
        *ppData = NULL;
        if (!name) return DXFILEERR_NOTFOUND;
        for (size_t i = 0; i < m_shared->file->roots.size(); ++i) {
            if (m_shared->file->roots[i]->name == name) {
                *ppData = new RanXFileData(m_shared, m_shared->file->roots[i]);
                return DXFILE_OK;
            }
        }
        return DXFILEERR_NOTFOUND;
    }
};

class RanXFile : public IDirectXFile {
public:
    LONG m_ref;
    RanXFile() : m_ref(1) {}

    HRESULT __stdcall QueryInterface(REFIID, void **ppv) { *ppv = this; AddRef(); return S_OK; }
    ULONG   __stdcall AddRef() { return (ULONG)++m_ref; }
    ULONG   __stdcall Release() { LONG r = --m_ref; if (r <= 0) { delete this; return 0; } return (ULONG)r; }

    HRESULT __stdcall CreateEnumObject(LPVOID pvSource, DXFILELOADOPTIONS options,
                                       LPDIRECTXFILEENUMOBJECT *ppEnumObj) {
        if (!ppEnumObj) return DXFILEERR_BADVALUE;
        *ppEnumObj = NULL;

        std::vector<BYTE> bytes;
        const void *data = NULL;
        size_t size = 0;

        if (options == DXFILELOAD_FROMFILE) {
            if (!pvSource || !readWhole((const char *)pvSource, bytes)) return DXFILEERR_FILENOTFOUND;
            data = &bytes[0];
            size = bytes.size();
        } else if (options == DXFILELOAD_FROMMEMORY) {
            const DXFILELOADMEMORY *mem = (const DXFILELOADMEMORY *)pvSource;
            if (!mem || !mem->lpMemory) return DXFILEERR_BADVALUE;
            data = mem->lpMemory;
            size = mem->dSize;
        } else {
            return DXFILEERR_BADVALUE;
        }

        XFile *parsed = XFile_Parse(data, size);
        if (!parsed) return DXFILEERR_BADFILETYPE;

        *ppEnumObj = new RanXFileEnum(new SharedFile(parsed));
        return DXFILE_OK;
    }
    HRESULT __stdcall CreateSaveObject(LPCSTR, DXFILEFORMAT, LPDIRECTXFILESAVEOBJECT *ppSaveObj) {
        if (ppSaveObj) *ppSaveObj = NULL;
        return E_NOTIMPL;
    }
    HRESULT __stdcall RegisterTemplates(LPVOID, DWORD) { return DXFILE_OK; }
};

} // namespace

XNode *RanXFile_NodeOf(ID3DXFileData *data) {
    // The mesh loader is handed an ID3DXFileData by the engine and needs the
    // parsed node behind it. Only objects this file created ever reach here.
    return data ? ((RanD3DXFileData *)data)->m_node : NULL;
}

extern "C" HRESULT WINAPI D3DXFileCreate(LPD3DXFILE *ppFile) {
    if (!ppFile) return D3DERR_INVALIDCALL;
    *ppFile = new RanD3DXFile();
    return D3D_OK;
}

extern "C" HRESULT WINAPI DirectXFileCreate(LPDIRECTXFILE *ppFile) {
    if (!ppFile) return DXFILEERR_BADVALUE;
    *ppFile = new RanXFile();
    return DXFILE_OK;
}
