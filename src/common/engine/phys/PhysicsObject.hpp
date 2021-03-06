#ifndef _ENGINE_PHYS_PHYSICSOBJECT_H_
#define _ENGINE_PHYS_PHYSICSOBJECT_H_

#include "Types.hpp"
#include <glm/glm.hpp>

namespace vox {
    namespace engine {
        class World;

        namespace physics {
            class PhysicsObject {
                protected:
                    AABB _aabb;
                    glm::vec3 _lastPos;
                    glm::vec3 _vel;
                    glm::vec3 _acc;
                    bool _onground;
                    float _mass;
                    int _id;
                    //We don't collide with parents.
                    int _parentID;
                public:
                    PhysicsObject (const glm::vec3& Pos, const glm::vec3 Size, float Mass);
                    PhysicsObject (const glm::vec3& Pos, const glm::vec3 Size, float Mass, const PhysicsObject& Parent);
                    
                    void DoPhysics(World& In);
                    void ApplyForce(const glm::vec3& Force);
                    void ResolveCollision(PhysicsObject& Other);

                    //Ment to be overriden by subclasses interested in collisions with other entities.
                    virtual void OnCollision(const PhysicsObject& With) {}
                    //Ment to be overriden by subclasses interested in collisions with the world.
                    virtual void OnWorldHit(int X, int Y, int Z) {}
                    
                    float GetMass() const;
                    bool IsOnGround() const;

                    glm::vec3 GetPosition() const;
                    glm::vec3 GetSize() const;

                    inline bool Intersects(PhysicsObject Other) {
                        if (Other._id == _parentID || Other._parentID == _id)
                            return false;
                        return _aabb.Intersects(Other._aabb);
                    }
            };
        }
    }
}

#endif
