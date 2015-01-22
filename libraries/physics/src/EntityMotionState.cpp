//
//  EntityMotionState.cpp
//  libraries/entities/src
//
//  Created by Andrew Meadows on 2014.11.06
//  Copyright 2013 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include <EntityItem.h>
#include <EntityEditPacketSender.h>

#include "BulletUtil.h"
#include "EntityMotionState.h"
#include "SimpleEntityKinematicController.h"


QSet<EntityItem*>* _outgoingEntityList;

// static 
void EntityMotionState::setOutgoingEntityList(QSet<EntityItem*>* list) {
    assert(list);
    _outgoingEntityList = list;
}

// static 
void EntityMotionState::enqueueOutgoingEntity(EntityItem* entity) {
    assert(_outgoingEntityList);
    _outgoingEntityList->insert(entity);
}

EntityMotionState::EntityMotionState(EntityItem* entity) 
    :   _entity(entity) {
    _type = MOTION_STATE_TYPE_ENTITY;
    assert(entity != NULL);
}

EntityMotionState::~EntityMotionState() {
    assert(_entity);
    _entity->setPhysicsInfo(NULL);
    _entity = NULL;
    delete _kinematicController;
    _kinematicController = NULL;
}

MotionType EntityMotionState::computeMotionType() const {
    if (_entity->getCollisionsWillMove()) {
        return MOTION_TYPE_DYNAMIC;
    }
    return _entity->isMoving() ?  MOTION_TYPE_KINEMATIC : MOTION_TYPE_STATIC;
}

void EntityMotionState::addKinematicController() {
    if (!_kinematicController) {
        _kinematicController = new SimpleEntityKinematicController(_entity);
        _kinematicController->start();
    } else {
        _kinematicController->start();
    }
}

// This callback is invoked by the physics simulation in two cases:
// (1) when the RigidBody is first added to the world
//     (irregardless of MotionType: STATIC, DYNAMIC, or KINEMATIC)
// (2) at the beginning of each simulation frame for KINEMATIC RigidBody's --
//     it is an opportunity for outside code to update the object's simulation position
void EntityMotionState::getWorldTransform(btTransform& worldTrans) const {
    if (_kinematicController && _kinematicController->isRunning()) {
        _kinematicController->stepForward();
    }
    worldTrans.setOrigin(glmToBullet(_entity->getPositionInMeters() - ObjectMotionState::getWorldOffset()));
    worldTrans.setRotation(glmToBullet(_entity->getRotation()));
}

// This callback is invoked by the physics simulation at the end of each simulation frame...
// iff the corresponding RigidBody is DYNAMIC and has moved.
void EntityMotionState::setWorldTransform(const btTransform& worldTrans) {
    _entity->setPositionInMeters(bulletToGLM(worldTrans.getOrigin()) + ObjectMotionState::getWorldOffset());
    _entity->setRotation(bulletToGLM(worldTrans.getRotation()));

    glm::vec3 v;
    getVelocity(v);
    _entity->setVelocityInMeters(v);

    getAngularVelocity(v);
    // DANGER! EntityItem stores angularVelocity in degrees/sec!!!
    _entity->setAngularVelocity(glm::degrees(v));

    _outgoingPacketFlags = DIRTY_PHYSICS_FLAGS;
    EntityMotionState::enqueueOutgoingEntity(_entity);
}

void EntityMotionState::updateObjectEasy(uint32_t flags, uint32_t frame) {
    if (flags & (EntityItem::DIRTY_POSITION | EntityItem::DIRTY_VELOCITY)) {
        if (flags & EntityItem::DIRTY_POSITION) {
            _sentPosition = _entity->getPositionInMeters() - ObjectMotionState::getWorldOffset();
            btTransform worldTrans;
            worldTrans.setOrigin(glmToBullet(_sentPosition));

            _sentRotation = _entity->getRotation();
            worldTrans.setRotation(glmToBullet(_sentRotation));

            _body->setWorldTransform(worldTrans);
        }
        if (flags & EntityItem::DIRTY_VELOCITY) {
            updateObjectVelocities();
        }
        _sentFrame = frame;
    }

    // TODO: entity support for friction and restitution
    //_restitution = _entity->getRestitution();
    _body->setRestitution(_restitution);
    //_friction = _entity->getFriction();
    _body->setFriction(_friction);

    _linearDamping = _entity->getDamping();
    _angularDamping = _entity->getAngularDamping();
    _body->setDamping(_linearDamping, _angularDamping);

    if (flags & EntityItem::DIRTY_MASS) {
        float mass = _entity->computeMass();
        btVector3 inertia(0.0f, 0.0f, 0.0f);
        _body->getCollisionShape()->calculateLocalInertia(mass, inertia);
        _body->setMassProps(mass, inertia);
        _body->updateInertiaTensor();
    }
    _body->activate();
};

void EntityMotionState::updateObjectVelocities() {
    if (_body) {
        _sentVelocity = _entity->getVelocityInMeters();
        setVelocity(_sentVelocity);

        // DANGER! EntityItem stores angularVelocity in degrees/sec!!!
        _sentAngularVelocity = glm::radians(_entity->getAngularVelocity());
        setAngularVelocity(_sentAngularVelocity);

        _sentAcceleration = _entity->getGravityInMeters();
        setGravity(_sentAcceleration);

        _body->setActivationState(ACTIVE_TAG);
    }
}

void EntityMotionState::computeShapeInfo(ShapeInfo& shapeInfo) {
    _entity->computeShapeInfo(shapeInfo);
}

float EntityMotionState::computeMass(const ShapeInfo& shapeInfo) const {
    return _entity->computeMass();
}

void EntityMotionState::sendUpdate(OctreeEditPacketSender* packetSender, uint32_t frame) {
    if (!_entity->isKnownID()) {
        return; // never update entities that are unknown
    }
    if (_outgoingPacketFlags) {
        EntityItemProperties properties = _entity->getProperties();

        if (_outgoingPacketFlags & EntityItem::DIRTY_POSITION) {
            btTransform worldTrans = _body->getWorldTransform();
            _sentPosition = bulletToGLM(worldTrans.getOrigin());
            properties.setPosition(_sentPosition + ObjectMotionState::getWorldOffset());
        
            _sentRotation = bulletToGLM(worldTrans.getRotation());
            properties.setRotation(_sentRotation);
        }
    
        if (_outgoingPacketFlags & EntityItem::DIRTY_VELOCITY) {
            if (_body->isActive()) {
                _sentVelocity = bulletToGLM(_body->getLinearVelocity());
                _sentAngularVelocity = bulletToGLM(_body->getAngularVelocity());

                // if the speeds are very small we zero them out
                const float MINIMUM_EXTRAPOLATION_SPEED_SQUARED = 1.0e-4f; // 1cm/sec
                bool zeroSpeed = (glm::length2(_sentVelocity) < MINIMUM_EXTRAPOLATION_SPEED_SQUARED);
                if (zeroSpeed) {
                    _sentVelocity = glm::vec3(0.0f);
                }
                const float MINIMUM_EXTRAPOLATION_SPIN_SQUARED = 0.004f; // ~0.01 rotation/sec
                bool zeroSpin = glm::length2(_sentAngularVelocity) < MINIMUM_EXTRAPOLATION_SPIN_SQUARED;
                if (zeroSpin) {
                    _sentAngularVelocity = glm::vec3(0.0f);
                }

                _sentMoving = ! (zeroSpeed && zeroSpin);
            } else {
                _sentVelocity = _sentAngularVelocity = glm::vec3(0.0f);
                _sentMoving = false;
            }
            properties.setVelocity(_sentVelocity);
            _sentAcceleration = bulletToGLM(_body->getGravity());
            properties.setGravity(_sentAcceleration);
            // DANGER! EntityItem stores angularVelocity in degrees/sec!!!
            properties.setAngularVelocity(glm::degrees(_sentAngularVelocity));
        }

        // RELIABLE_SEND_HACK: count number of updates for entities at rest so we can stop sending them after some limit.
        if (_sentMoving) {
            _numNonMovingUpdates = 0;
        } else {
            _numNonMovingUpdates++;
        }
        if (_numNonMovingUpdates <= 1) {
            // we only update lastEdited when we're sending new physics data 
            // (i.e. NOT when we just simulate the positions forward, nore when we resend non-moving data)
            quint64 lastSimulated = _entity->getLastSimulated();
            _entity->setLastEdited(lastSimulated);
            properties.setLastEdited(lastSimulated);
        } else {
            properties.setLastEdited(_entity->getLastEdited());
        }

        EntityItemID id(_entity->getID());
        EntityEditPacketSender* entityPacketSender = static_cast<EntityEditPacketSender*>(packetSender);
        entityPacketSender->queueEditEntityMessage(PacketTypeEntityAddOrEdit, id, properties);

        // The outgoing flags only itemized WHAT to send, not WHETHER to send, hence we always set them
        // to the full set.  These flags may be momentarily cleared by incoming external changes.  
        _outgoingPacketFlags = DIRTY_PHYSICS_FLAGS;
        _sentFrame = frame;
    }
}

uint32_t EntityMotionState::getIncomingDirtyFlags() const { 
    uint32_t dirtyFlags = _entity->getDirtyFlags(); 

    // we add DIRTY_MOTION_TYPE if the body's motion type disagrees with entity velocity settings
    int bodyFlags = _body->getCollisionFlags();
    bool isMoving = _entity->isMoving();
    if (((bodyFlags & btCollisionObject::CF_STATIC_OBJECT) && isMoving) ||
            (bodyFlags & btCollisionObject::CF_KINEMATIC_OBJECT && !isMoving)) {
        dirtyFlags |= EntityItem::DIRTY_MOTION_TYPE; 
    }
    return dirtyFlags;
}