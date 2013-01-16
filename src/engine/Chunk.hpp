#ifndef _ENGINE_CHUNK_H_
#define _ENGINE_CHUNK_H_

#include "WorldGenerator.hpp"

#define CHUNK_SIZE 16

namespace vox {
    namespace engine {
        class Chunk {
            private:
                int _x, _y, _z;
                unsigned char* _data;
            public:
                Chunk(int X, int Y, int Z, WorldGenerator* Gen);
                Chunk(const Chunk& Other);
                ~Chunk();

                unsigned char GetBlock(int X, int Y, int Z) const;
                unsigned char& GetBlock (int X, int Y, int Z);

                inline int GetX() {return _x;}
                inline int GetY() {return _y;}
                inline int GetZ() {return _z;}
        };
    }
}

#endif
