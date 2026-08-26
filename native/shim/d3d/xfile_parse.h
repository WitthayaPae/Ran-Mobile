// DirectX .x container reader — the tree, without any COM.
//
// The client ships three flavours (counted across CLIENT/data): `bin` binary
// token stream, `bzip` the same stream MSZip-compressed, and `txt`. Skeletons,
// skins and map objects each use all three, so all three are read here.
//
// What a caller gets is a plain tree: every data object keeps its template
// name/GUID, its optional instance name, its children, and its members packed
// in stream order — which is the layout D3DX hands back from Lock(), and what
// the engine casts to D3DXMATRIX and friends.
#pragma once

#include "windows.h"
#include <string>
#include <vector>

struct XNode {
    GUID                 type;          // template GUID, zeroed if unknown
    std::string          typeName;      // template name, e.g. "Mesh"
    std::string          name;          // instance name, may be empty
    std::vector<BYTE>    data;          // members packed in stream order
    std::vector<XNode *> children;      // owned
    XNode               *reference;     // set for `{ name }` references
    XNode               *parent;

    XNode() : reference(NULL), parent(NULL) { memset(&type, 0, sizeof(type)); }
    ~XNode() { for (size_t i = 0; i < children.size(); ++i) delete children[i]; }
};

struct XFile {
    std::vector<XNode *> roots;                 // owned
    std::vector<std::string *> strings;         // owned; data blobs point into these
    ~XFile() {
        for (size_t i = 0; i < roots.size(); ++i) delete roots[i];
        for (size_t i = 0; i < strings.size(); ++i) delete strings[i];
    }
};

// Parses a whole .x file from memory. Returns NULL if the bytes are not a .x
// file this reader understands; the reason is logged once per distinct cause.
XFile *XFile_Parse(const void *data, size_t size);

// Looks up a template GUID by its name, for the standard D3DRM templates.
bool XFile_GuidForTemplate(const char *name, GUID *out);
