//
//   Copyright 2013 Pixar
//
//   Licensed under the Apache License, Version 2.0 (the "Apache License")
//   with the following modification; you may not use this file except in
//   compliance with the Apache License and the following modification to it:
//   Section 6. Trademarks. is deleted and replaced with:
//
//   6. Trademarks. This License does not grant permission to use the trade
//      names, trademarks, service marks, or product names of the Licensor
//      and its affiliates, except as required to comply with Section 4(c) of
//      the License and to reproduce the content of the NOTICE file.
//
//   You may obtain a copy of the Apache License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
//   Unless required by applicable law or agreed to in writing, software
//   distributed under the Apache License with the above modification is
//   distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
//   KIND, either express or implied. See the Apache License for the specific
//   language governing permissions and limitations under the Apache License.
//

#include <far/meshFactory.h>

#include "refiner.h"

#include <osd/vertex.h>

#include <fstream>

using namespace OpenSubdiv;
using namespace std;


//------------------------------------------------------------------------------
// The simplest constructor, only point positions and polygonal
// mesh topology


PxOsdUtilRefiner::PxOsdUtilRefiner():
    _adaptive(false),
    _mesh(NULL),
    _farMesh(NULL),
    _patchParamTable(NULL),
    _firstVertexOffset(0),
    _numRefinedVerts(0),
    _numUniformQuads(0),
    _numPatches(0),        
    _level(1),
    _isRefined(false)
{
}

    
bool
PxOsdUtilRefiner::Initialize(
       const PxOsdUtilSubdivTopology &topology,
       bool adaptive,
       string *errorMessage)    
{

    std::cout << "Initializing refiner\n";

    if (not topology.IsValid(errorMessage)) {
        std::cout << "Topology invalid:\n\t" << *errorMessage << "\n";
        topology.Print();
        return false;
    } else {
        std::cout << "Topology is valid\n";
    }
         
        

    _mesh = new PxOsdUtilMesh(topology, errorMessage);

    std::cout << "\tCreated _mesh in refiner\n";
    
    if (not _mesh->IsValid()) {
        std::cout << "Invalid mesh\n";
        return false;
    }

    const PxOsdUtilSubdivTopology &t = _mesh->GetTopology();

    if (adaptive) {
        std::cout << "\tAdaptive mesh for refiner\n";
        FarMeshFactory<OsdVertex> adaptiveMeshFactory(
            _mesh->GetHbrMesh(), t.maxLevels, true);
        
        _farMesh = adaptiveMeshFactory.Create();
        
    } else {
        std::cout << "\tUniform mesh for refiner, maxLevels = " << t.maxLevels << "\n";

        HbrMesh<OsdVertex> *hmesh = _mesh->GetHbrMesh();
        
        t.Print();
        
        std::cout << "\tHbr mesh has faces " << hmesh->GetNumFaces() << "  " << hmesh->GetNumCoarseFaces() << " and vertices " <<
            hmesh->GetNumVertices() << ", disconnected = " <<
            hmesh->GetNumDisconnectedVertices() << "\n";

    hmesh->PrintStats(std::cout);

        
        // create the quad tables to include all levels by specifying
        // firstLevel as 1
        FarMeshFactory<OsdVertex> uniformMeshFactory(
            _mesh->GetHbrMesh(), t.maxLevels, false, /*firstLevel=*/1);

        _farMesh = uniformMeshFactory.Create();

        std::cout << "\tUniform farmesh created with " << t.maxLevels << "\n";

    }

    //
    // Now that we've created table driven subdivision data structures
    // needed for refinement,  grab and cache specific values for
    // later fast lookup.
    //

    // Subdivision tables describe the addition steps with coefficients
    // needed to perform subdivision
    const FarSubdivisionTables<OsdVertex>* ftable =
        _farMesh->GetSubdivisionTables();
    
    // Find quads array at _level
    const FarPatchTables * ptables = _farMesh->GetPatchTables();
    const FarPatchTables::PatchArrayVector & parrays =
        ptables->GetPatchArrayVector();
    
    if (_level > (int)parrays.size()) {
/*XXX        
         *errorMessage = TfStringPrintf(
                 "Invalid size of patch array %d %d\n",
                 _level, (int)parrays.size());;
*/                 
        return false;
    }

    // parrays doesn't contain base mesh, so it starts with level==1
    const FarPatchTables::PatchArray & parray = parrays[_level-1];

    _patchParamTable = &(ptables->GetPatchParamTable());

    // Global index of the first point in this array
    _firstVertexOffset =  ftable->GetFirstVertexOffset(_level);
    
    // Global index of the first face (patch) in this array
    _firstPatchOffset =  parray.GetPatchIndex();

    _numRefinedVerts = (int) ftable->GetNumVertices(_level);

    std::cout << "refiner has " << _numRefinedVerts << " refined verts\n";
    if (adaptive) {
        _numPatches = (int) parray.GetNumPatches();
    } else {
        _numUniformQuads = (int) parray.GetNumPatches();
    }
    
    _isRefined = true;
    
    return true;
}

bool
PxOsdUtilRefiner::GetRefinedQuads(
    vector<int>* quads,
    string *errorMessage) const
{
    if (!_isRefined) {
        if (errorMessage) {
            *errorMessage = "GetQuads: Mesh has not been refined.";
        }
        return false;
    }

    if (_adaptive) {
        if (errorMessage) {
            *errorMessage = "GetQuads: only supports uniform subdivision.";
        }
        return false;        
    }
    
    if (!quads || (_numUniformQuads == 0)) {
        return false;
    }
    
    quads->resize(_numUniformQuads * 4);

    const FarPatchTables * ptables = _farMesh->GetPatchTables();
    const unsigned int *quadIndices = ptables->GetFaceVertices(_level);

    for (int i=0; i<_numUniformQuads*4; ++i) {
        (*quads)[i] = quadIndices[i] - _firstVertexOffset;
    }

    return true;
}

// Inverse of OpenSubdiv::FarPatchParam::BitField::Normalize
static void
_InverseNormalize(OpenSubdiv::FarPatchParam::BitField bf, float& u, float& v)
{
    float frac = bf.GetParamFraction();
    float pu = (float) bf.GetU() * frac;
    float pv = (float) bf.GetV() * frac;

    u = u * frac + pu;
    v = v * frac + pv;
}

// Inverse of OpenSubdiv::FarPatchParam::BitField::Rotate
static void 
_InverseRotate(OpenSubdiv::FarPatchParam::BitField bf, float& u, float& v)
{
    switch (bf.GetRotation()) {
         case 0 : break;
         case 1 : { float tmp=u; u=1.0f-v; v=tmp; } break;
         case 2 : { u=1.0f-u; v=1.0f-v; } break;
         case 3 : { float tmp=v; v=1.0f-u; u=tmp; } break;
    }
}

bool
PxOsdUtilRefiner::GetRefinedPtexUvs(vector<float>* subfaceUvs,
                                    vector<int>* ptexIndices,
                                    string *errorMessage) const
{
    if (!_isRefined) {
        if (errorMessage) {
            *errorMessage = "GetRefinedPtexUvs: Mesh has not been refined.";
        }
        return false;
    }

    if (_adaptive) {
        if (errorMessage) {
            *errorMessage = "GetRefinedPtexUvs: only supports uniform subdivision.";
        }
        return false;        
    }

    subfaceUvs->resize(_numUniformQuads * 4);
    vector<float>::iterator uvIt = subfaceUvs->begin();

    ptexIndices->resize(_numUniformQuads);
    vector<int>::iterator idIt = ptexIndices->begin();

    const FarPatchTables * ptables = _farMesh->GetPatchTables();
    const FarPatchTables::PatchArrayVector & parrays =
        ptables->GetPatchArrayVector();
    if (_level > (int)parrays.size()) {
        if (errorMessage)
            *errorMessage = "Invalid size of patch array";
    }
    const FarPatchTables::PatchArray & parray = parrays[_level-1];
    
    const FarPatchTables::PatchParamTable& paramTable =
        ptables->GetPatchParamTable();
    
    for (int refinedIndex = 0; refinedIndex < _numUniformQuads;
         ++refinedIndex) {
        
        const OpenSubdiv::FarPatchParam& param =
            paramTable[parray.GetPatchIndex() + refinedIndex];
        OpenSubdiv::FarPatchParam::BitField bf = param.bitField;
        
        float u0 = 0;
        float v0 = 0;
        _InverseRotate(bf, u0, v0);
        _InverseNormalize(bf, u0, v0);
        
        float u1 = 1;
        float v1 = 1;
        _InverseRotate(bf, u1, v1);
        _InverseNormalize(bf, u1, v1);
        
        *idIt++ = param.faceIndex;
        *uvIt++ = u0;
        *uvIt++ = v0;
        *uvIt++ = u1;
        *uvIt++ = v1;
    }

    return true;
}


PxOsdUtilRefiner::~PxOsdUtilRefiner() {
    
    if (_mesh)
        delete _mesh;

    if (_farMesh)
        delete _farMesh;
}


const std::string &
PxOsdUtilRefiner::GetName()
{
    if (_mesh) {
        return _mesh->GetName();
    } else {
        static std::string bogus("bogus");
        return bogus;
    }
}

OpenSubdiv::HbrMesh<OpenSubdiv::OsdVertex>*
PxOsdUtilRefiner::GetHbrMesh()
{
    if (_mesh) {
        return _mesh->GetHbrMesh();
    } else {
        return NULL;
    }
}


