#include "WorldRenderer.hpp"
#include <GL/glew.h>
#include "platform/Timer.hpp"
#include "engine/World.hpp"
#include "./gl/Shader.hpp"
#include "./gl/Util.hpp"
#include "GraphicsDefs.hpp"
#include "MathUtil.hpp"
#include <cstring>
#include <list>
#include <algorithm>

#include <iostream>

#define VIEWDIST 8
#define HALFDIST (VIEWDIST / 2)

using namespace std;
using namespace vox::ren;
using namespace vox::ren::gl;
using namespace vox::engine;
using namespace vox::engine::entity;

struct OnBlockSetHandler {
    WorldRenderer& Ren;
    OnBlockSetHandler(WorldRenderer& For) : Ren(For) {
    }

    void operator() (int X, int Y, int Z, unsigned char Val) {
        Ren.MarkBlockDirty(X, Y, Z);
    }
};

struct OnEntityAddHandler {
    WorldRenderer& Ren;
    OnEntityAddHandler(WorldRenderer& For) : Ren(For) {
    }

    void operator() (Entity* Ent) {
        Ren.AddEntityRenderer(new EntityRenderer(*Ent));
    }
};

struct OnEntityRemoveHandler {
    WorldRenderer& Ren;
    OnEntityRemoveHandler(WorldRenderer& For) : Ren(For) {
    }

    void operator() (Entity* Ent) {
        Ren.RemoveRendererForEntity(Ent);
    }
};

static inline int GetInd(int X, int Y, int Z) {
    int x = X % VIEWDIST;
    if (x < 0)
        x += VIEWDIST;
    int y = Y % VIEWDIST;
    if (y < 0)
        y += VIEWDIST;
    int z = Z % VIEWDIST;
    if (z < 0)
        z += VIEWDIST;
    return x + y * VIEWDIST + z * VIEWDIST * VIEWDIST;
}

WorldRenderer::WorldRenderer(World& For) : 
    _for(For),
    _basic(*(new ShaderProgram("res/base.frag", "res/base.vert"))),
    _man(FOV, ASPECT),
    _cameraPos(0, 24, 0) {
    _chunks = new RenderChunk*[VIEWDIST * VIEWDIST * VIEWDIST];
    _pitch = _yaw = _roll = 0;

    std::cout << "Nulling out chunks." << std::endl;
    memset(_chunks, (int)NULL, VIEWDIST * VIEWDIST * VIEWDIST * sizeof(RenderChunk*));

    For.OnBlockSet.connect(OnBlockSetHandler(*this));
    For.OnAddEntity.connect(OnEntityAddHandler(*this));
    For.OnRemoveEntity.connect(OnEntityRemoveHandler(*this));

    string m("MView");
    string p("MProj");
    _mloc = _basic.GetUniformLoc(m);
    _ploc = _basic.GetUniformLoc(p);

    _man.SetLocations(_mloc, _ploc);

    glPointSize(5.0f);
    glEnable(GL_DEPTH_TEST);
}

WorldRenderer::~WorldRenderer() {
    for (int i = 0; i < VIEWDIST * VIEWDIST * VIEWDIST; ++i) {
        delete _chunks[i];
    }
    delete[] _chunks;
}

static inline int ManhatanDistance(int x1, int y1, int z1, int x2, int y2, int z2) {
    return
        abs(x1 - x2) +
        abs(y1 - y2) +
        abs(z1 - z2);
}

void WorldRenderer::Render() {
    _man.PushMatrix();
    _man.Rotate(-_pitch, -_yaw, -_roll);
    _man.Translate(-_cameraPos.x, -_cameraPos.y, -_cameraPos.z);
    
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    PrintGLError("Prerender");
    _basic.Use();
    PrintGLError("WorldRenderer::Render");

    _man.SetLocations(_mloc, _ploc);
    _man.ToGPU();

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);

    unsigned int stime = vox::platform::CurrentTime(); 
    unsigned int elapsed = 0;
    for (int x = 0; x < VIEWDIST; ++x) {
        for (int y = 0; y < VIEWDIST; ++y) {
            for (int z = 0; z < VIEWDIST; ++z) {
                int cx = x + int(_cameraPos.x / CHUNK_SIZE) - HALFDIST;
                int cy = y + int(_cameraPos.y / CHUNK_SIZE) - HALFDIST;
                int cz = z + int(_cameraPos.z / CHUNK_SIZE) - HALFDIST;
                int magic = ManhatanDistance(x, y, z, HALFDIST, HALFDIST, HALFDIST) - 5;
                if (magic < 0)
                    magic = 0;
                magic = 0;
                int lod = CHUNK_SIZE >> magic;
                if (lod == 0)
                    lod = 1;
                int ind = GetInd(cx, cy, cz);
                RenderChunk* curr = _chunks[ind];
                if (curr == NULL) {
                    ToBuildChunk temp;
                    temp.X = cx;
                    temp.Y = cy;
                    temp.Z = cz;
                    temp.LOD = lod;
                    temp.Ind = ind;
                    temp.Parent = this;
                    _toBuild.insert(temp);
                    curr = NULL;
                } else if (
                        curr->GetLOD() != lod ||
                        curr->GetX() != cx ||
                        curr->GetY() != cy ||
                        curr->GetZ() != cz ||
                        curr->IsDirty()) {
                    ToBuildChunk temp;
                    temp.X = cx;
                    temp.Y = cy;
                    temp.Z = cz;
                    temp.LOD = lod;
                    temp.Ind = ind;
                    temp.Parent = this;
                    _toBuild.insert(temp);
                }
                if (curr != NULL)
                    curr->Render();
                elapsed = vox::platform::CurrentTime() - stime;
                //8ms
#ifndef VALGRIND
                if (elapsed > 8000) {
                    goto bail;
                }
#endif
            }
        }
    }
bail:
    //XXX:This prevents valgrind from letting me profile the game. Too slow...
    if (!_toBuild.empty()) {
        ToBuildSet::iterator it = _toBuild.begin();
        while (
#ifndef VALGRIND
                elapsed < 8000 &&
#endif
                it != _toBuild.end()) {
            ToBuildChunk c = *it;
            ToBuildSet::iterator curr = it++;
            _toBuild.erase(curr);
            delete _chunks[c.Ind];
            _chunks[c.Ind] = NULL;
            _chunks[c.Ind] = new RenderChunk(c.X, c.Y, c.Z, c.LOD, _for);
            elapsed = vox::platform::CurrentTime() - stime;
        }
    }

    for (
        std::list<EntityRenderer*>::iterator it = _ents.begin();
        it != _ents.end();
        ++it) {
        (*it)->Render(&_man);
    }

    _man.PopMatrix();
    
    PrintGLError("Postrender");
    glFlush();
}

TransformationManager* WorldRenderer::GetTranslationManager() {
    return &_man;
}

void WorldRenderer::SetCameraPosition(glm::vec3 Vec) {
    _cameraPos = Vec;
}

void WorldRenderer::SetCameraDirection(float Yaw, float Pitch, float Roll) {
    _yaw = Yaw;
    _pitch = Pitch;
    _roll = Roll;
}

void WorldRenderer::MarkBlockDirty(int X, int Y, int Z) {
    int cx = vox::Floor(X / float(CHUNK_SIZE));
    int cy = vox::Floor(Y / float(CHUNK_SIZE));
    int cz = vox::Floor(Z / float(CHUNK_SIZE));
    RenderChunk* tmp = NULL;
    tmp = _chunks[GetInd(cx, cy, cz)];
    if (tmp != NULL) tmp->MarkDirty();
    //Handle *puts on sunglasses* edge cases.
    int lx = X % CHUNK_SIZE;
    if (lx < 0)
        lx += CHUNK_SIZE;
    int ly = Y % CHUNK_SIZE;
    if (ly < 0)
        ly += CHUNK_SIZE;
    int lz = Z % CHUNK_SIZE;
    if (lz < 0)
        lz += CHUNK_SIZE;
    if (lx == 0) {
        tmp = _chunks[GetInd(cx - 1, cy, cz)];
        if (tmp != NULL) tmp->MarkDirty();
    } else if (lx == CHUNK_SIZE - 1) {
        tmp = _chunks[GetInd(cx + 1, cy, cz)];
        if (tmp != NULL) tmp->MarkDirty();
    }
    if (ly == 0) {
        tmp = _chunks[GetInd(cx, cy - 1, cz)];
        if (tmp != NULL) tmp->MarkDirty();
    } else if (ly == CHUNK_SIZE - 1) {
        tmp = _chunks[GetInd(cx, cy + 1, cz)];
        if (tmp != NULL) tmp->MarkDirty();
    }
    if (lz == 0) {
        tmp = _chunks[GetInd(cx, cy, cz - 1)];
        if (tmp != NULL) tmp->MarkDirty();
    } else if (lz == CHUNK_SIZE - 1) {
        tmp = _chunks[GetInd(cx, cy, cz + 1)];
        if (tmp != NULL) tmp->MarkDirty();
    }
}

void WorldRenderer::AddEntityRenderer(EntityRenderer* Ent) {
    _ents.push_front(Ent);
}

void WorldRenderer::RemoveEntityRenderer(EntityRenderer* Ent) {
    _ents.erase(find(_ents.begin(), _ents.end(), Ent));
}

void WorldRenderer::RemoveRendererForEntity(vox::engine::entity::Entity* Ent) {
    std::list<EntityRenderer*>::iterator it = _ents.begin();
    while (it != _ents.end()) {
        std::list<EntityRenderer*>::iterator curr = it++;
        if ((*curr)->IsRendererFor(Ent)) {
            _ents.erase(curr);
        }
    }
}

//----------------------------------
//      ToBuildChunk Members.
//----------------------------------

bool ToBuildChunk::operator< (const ToBuildChunk& Other) const {
    //Positions must be in chunk coords.
    float camx = Parent->_cameraPos.x / CHUNK_SIZE;
    float camy = 1;
    float camz = Parent->_cameraPos.y / CHUNK_SIZE;

    return ManhatanDistance(X, Y, Z, camx, camy, camz) <
        ManhatanDistance(Other.X, Other.Y, Other.Z, camx, camy, camz);
}
