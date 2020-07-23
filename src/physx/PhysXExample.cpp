/*
    This file is part of Magnum.

    Original authors — credit is appreciated but not required:

        2010, 2011, 2012, 2013, 2014, 2015, 2016, 2017, 2018, 2019, 2020 —
            Vladimír Vondruš <mosra@centrum.cz>
        2020 — Jonathan Hale <squareys@googlemail.com>

    This is free and unencumbered software released into the public domain.

    Anyone is free to copy, modify, publish, use, compile, sell, or distribute
    this software, either in source code form or as a compiled binary, for any
    purpose, commercial or non-commercial, and by any means.

    In jurisdictions that recognize copyright laws, the author or authors of
    this software dedicate any and all copyright interest in the software to
    the public domain. We make this dedication for the benefit of the public
    at large and to the detriment of our heirs and successors. We intend this
    dedication to be an overt act of relinquishment in perpetuity of all
    present and future rights to this software under copyright law.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
    THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
    IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
    CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <Corrade/Containers/GrowableArray.h>
#include <Corrade/Containers/Optional.h>
#include <Corrade/Containers/Pointer.h>
#include <Magnum/Timeline.h>
#include <Magnum/GL/DefaultFramebuffer.h>
#include <Magnum/GL/Mesh.h>
#include <Magnum/GL/Renderer.h>
#include <Magnum/Math/Constants.h>
#include <Magnum/Math/Color.h>
#include <Magnum/Math/Quaternion.h>
#include <Magnum/MeshTools/Compile.h>
#include <Magnum/MeshTools/Transform.h>
#include <Magnum/Platform/Sdl2Application.h>
#include <Magnum/Primitives/Cube.h>
#include <Magnum/Primitives/UVSphere.h>
#include <Magnum/SceneGraph/Camera.h>
#include <Magnum/SceneGraph/Drawable.h>
#include <Magnum/SceneGraph/MatrixTransformation3D.h>
#include <Magnum/SceneGraph/Scene.h>
#include <Magnum/Shaders/Phong.h>
#include <Magnum/Trade/MeshData.h>

#include "PxPhysicsAPI.h"

/* Set this to the IP address of the system running the PhysX Visual Debugger
   that you want to connect to */
#define PVD_HOST "127.0.0.1"

using namespace physx;

/* The destructor of PxDefaultErrorCallback is not a public symbol,
 * it is meant to be defined as a global (???) */
PxDefaultErrorCallback _pxErrorCallback;

namespace Magnum { namespace Examples {

using namespace Math::Literals;

typedef SceneGraph::Object<SceneGraph::MatrixTransformation3D> Object3D;
typedef SceneGraph::Scene<SceneGraph::MatrixTransformation3D> Scene3D;

struct InstanceData {
    Matrix4 transformationMatrix;
    Matrix3x3 normalMatrix;
    Color3 color;
};

class PhysXExample: public Platform::Application {
    public:
        explicit PhysXExample(const Arguments& arguments);

    private:
        void drawEvent() override;
        void keyPressEvent(KeyEvent& event) override;
        void mousePressEvent(MouseEvent& event) override;

        GL::Mesh _box{NoCreate}, _sphere{NoCreate};
        GL::Buffer _boxInstanceBuffer{NoCreate}, _sphereInstanceBuffer{NoCreate};
        Shaders::Phong _shader{NoCreate};
        Containers::Array<InstanceData> _boxInstanceData, _sphereInstanceData;

        PxDefaultAllocator _pxAllocator;

        PxFoundation* _pxFoundation;
        Containers::Pointer<PxPhysics> _pxPhysics;

        Containers::Pointer<PxDefaultCpuDispatcher> _pxDispatcher;
        PxScene* _pxScene;
        PxMaterial* _pxMaterial;
        PxPvd* _pxPvd = nullptr;

        Scene3D _scene;
        SceneGraph::Camera3D* _camera;
        SceneGraph::DrawableGroup3D _drawables;
        Timeline _timeline;

        Object3D *_cameraRig, *_cameraObject;

        bool _shootBox = false;
};

class ColoredDrawable: public SceneGraph::Drawable3D {
    public:
        explicit ColoredDrawable(Object3D& object, Containers::Array<InstanceData>& instanceData, const Color3& color, const Matrix4& primitiveTransformation, SceneGraph::DrawableGroup3D& drawables): SceneGraph::Drawable3D{object, &drawables}, _instanceData(instanceData), _color{color}, _primitiveTransformation{primitiveTransformation} {}

    private:
        void draw(const Matrix4& transformation, SceneGraph::Camera3D&) override {
            const Matrix4 t = transformation*_primitiveTransformation;
            arrayAppend(_instanceData, Containers::InPlaceInit,
                t, t.normalMatrix(), _color);
        }

        Containers::Array<InstanceData>& _instanceData;
        Color3 _color;
        Matrix4 _primitiveTransformation;
};

class RigidBody: public Object3D {
    public:
        RigidBody(Object3D* parent, Float mass, PxPhysics& pxPhysics,
            PxScene& pxScene, PxShape& pxShape, const Matrix4& transform):
            Object3D{parent}
        {
            PxTransform t;
            Vector3 p = transform.translation();
            Quaternion q = Quaternion::fromMatrix(transform.rotationScaling());
            t.p = PxVec3{p.x(), p.y(), p.z()};
            t.q = PxQuat{q.vector().x(), q.vector().y(), q.vector().z(), q.scalar()};
            _pxRigidBody = pxPhysics.createRigidDynamic(t);
            _pxRigidBody->setAngularDamping(0.5f);
            _pxRigidBody->attachShape(pxShape);
            _pxRigidBody->userData = this;
            if(mass != 0.0f)
                PxRigidBodyExt::updateMassAndInertia(*_pxRigidBody, mass);
            pxScene.addActor(*_pxRigidBody);
        }

        void update(PxRigidActor* actor) {
            const PxTransform t = actor->getGlobalPose();
            const Vector3 pos = Vector3{t.p.x, t.p.y, t.p.z};
            const Quaternion rot = Quaternion{Vector3{t.q.x, t.q.y, t.q.z},
                t.q.w};
            setTransformation(Matrix4::from(rot.toMatrix(), pos));
        }

        PxRigidDynamic& rigidBody() {
            return *_pxRigidBody;
        }

    private:
        PxRigidDynamic* _pxRigidBody;
};

PhysXExample::PhysXExample(const Arguments& arguments): Platform::Application(arguments, NoCreate) {
    /* Try 8x MSAA, fall back to zero samples if not possible. Enable only 2x
       MSAA if we have enough DPI. */
    {
        const Vector2 dpiScaling = this->dpiScaling({});
        Configuration conf;
        conf.setTitle("Magnum PhysX Integration Example")
            .setSize(conf.size(), dpiScaling);
        GLConfiguration glConf;
        glConf.setSampleCount(dpiScaling.max() < 2.0f ? 8 : 2);
        if(!tryCreate(conf, glConf))
            create(conf, glConf.setSampleCount(0));
    }

    /* Camera setup */
    (*(_cameraRig = new Object3D{&_scene}))
        .translate(Vector3::yAxis(3.0f))
        .rotateY(40.0_degf);
    (*(_cameraObject = new Object3D{_cameraRig}))
        .translate(Vector3::zAxis(20.0f))
        .rotateX(-25.0_degf);
    (_camera = new SceneGraph::Camera3D(*_cameraObject))
        ->setAspectRatioPolicy(SceneGraph::AspectRatioPolicy::Extend)
        .setProjectionMatrix(Matrix4::perspectiveProjection(35.0_degf, 1.0f, 0.001f, 100.0f))
        .setViewport(GL::defaultFramebuffer.viewport().size());

    /* Create an instanced shader */
    _shader = Shaders::Phong{
        Shaders::Phong::Flag::VertexColor|
        Shaders::Phong::Flag::InstancedTransformation};
    _shader.setAmbientColor(0x111111_rgbf)
           .setSpecularColor(0x330000_rgbf)
           .setLightPosition({10.0f, 15.0f, 5.0f});

    /* Box and sphere mesh, with an (initially empty) instance buffer */
    _box = MeshTools::compile(Primitives::cubeSolid());
    _sphere = MeshTools::compile(Primitives::uvSphereSolid(16, 32));
    _boxInstanceBuffer = GL::Buffer{};
    _sphereInstanceBuffer = GL::Buffer{};
    _box.addVertexBufferInstanced(_boxInstanceBuffer, 1, 0,
        Shaders::Phong::TransformationMatrix{},
        Shaders::Phong::NormalMatrix{},
        Shaders::Phong::Color3{});
    _sphere.addVertexBufferInstanced(_sphereInstanceBuffer, 1, 0,
        Shaders::Phong::TransformationMatrix{},
        Shaders::Phong::NormalMatrix{},
        Shaders::Phong::Color3{});

    /* PhysX setup */
    _pxFoundation = PxCreateFoundation(
        PX_PHYSICS_VERSION, _pxAllocator, _pxErrorCallback);

#ifdef WITH_PVD
    _pxPvd = PxCreatePvd(*_pxFoundation);
    PxPvdTransport* transport = PxDefaultPvdSocketTransportCreate(
        PVD_HOST, 5425, 10);
    _pxPvd->connect(*transport, PxPvdInstrumentationFlag::eALL);
#endif

    _pxPhysics.reset(PxCreatePhysics(PX_PHYSICS_VERSION, *_pxFoundation,
        PxTolerancesScale(), true, _pxPvd));

    PxSceneDesc sceneDesc(_pxPhysics->getTolerancesScale());
    sceneDesc.gravity = PxVec3(0.0f, -9.81f, 0.0f);
    _pxDispatcher.reset(PxDefaultCpuDispatcherCreate(2));
    sceneDesc.cpuDispatcher = _pxDispatcher.get();
    sceneDesc.filterShader = PxDefaultSimulationFilterShader;
    _pxScene = _pxPhysics->createScene(sceneDesc);
    _pxScene->setFlag(PxSceneFlag::eENABLE_ACTIVE_ACTORS, true);

#ifdef WITH_PVD
    PxPvdSceneClient* pvdClient = _pxScene->getScenePvdClient();
    if(pvdClient) {
        pvdClient->setScenePvdFlag(PxPvdSceneFlag::eTRANSMIT_CONSTRAINTS, true);
        pvdClient->setScenePvdFlag(PxPvdSceneFlag::eTRANSMIT_CONTACTS, true);
        pvdClient->setScenePvdFlag(PxPvdSceneFlag::eTRANSMIT_SCENEQUERIES, true);
    }
#endif
    _pxMaterial = _pxPhysics->createMaterial(0.5f, 0.5f, 0.6f);

    /* Create the ground */
    PxRigidStatic* groundPlane = PxCreateStatic(*_pxPhysics,
        PxTransform{PxIDENTITY{}}, PxBoxGeometry(4.0f, 0.5f, 4.0f),
        *_pxMaterial);
    _pxScene->addActor(*groundPlane);
    auto* ground = new Object3D{&_scene};
    new ColoredDrawable{*ground, _boxInstanceData, 0xffffff_rgbf,
        Matrix4::scaling({4.0f, 0.5f, 4.0f}), _drawables};

    const Float halfExtent = 0.5f;
    PxShape* shape = _pxPhysics->createShape(
        PxBoxGeometry(halfExtent, halfExtent, halfExtent), *_pxMaterial);
    /* Create boxes with random colors */
    Deg hue = 42.0_degf;
    for(Int i = 0; i != 5; ++i) {
        for(Int j = 0; j != 5; ++j) {
            for(Int k = 0; k != 5; ++k) {
                auto* o = new RigidBody{&_scene, 1.0f,
                    *_pxPhysics, *_pxScene, *shape,
                    Matrix4::translation({i - 2.0f, j + 4.0f, k - 2.0f})};
                new ColoredDrawable{*o, _boxInstanceData,
                    Color3::fromHsv({hue += 137.5_degf, 0.75f, 0.9f}),
                    Matrix4::scaling(Vector3{0.5f}), _drawables};
            }
        }
    }
    shape->release();

    /* Loop at 60 Hz max */
    setSwapInterval(1);
    setMinimalLoopPeriod(16);
    _timeline.start();

    GL::Renderer::enable(GL::Renderer::Feature::DepthTest);
}

void PhysXExample::drawEvent() {
    GL::defaultFramebuffer.clear(GL::FramebufferClear::Color|GL::FramebufferClear::Depth);

    /* Step physx simulation */
    _pxScene->simulate(Math::max(_timeline.previousFrameDuration(), 0.01f));
    _pxScene->fetchResults(true);

    /* Get transforms from PhysX */
    PxU32 actorCount = 0;
    PxActor** actors = _pxScene->getActiveActors(actorCount);

    PxU32 updatedCount = 0;
    for(PxActor* actor : Containers::arrayView(actors, actorCount)) {
        if(!actor->is<PxRigidActor>()) continue;

        reinterpret_cast<RigidBody*>(actor->userData)->update(reinterpret_cast<PxRigidActor*>(actor));
        ++updatedCount;
    }

    /* Populate instance data with transformations and colors */
    arrayResize(_boxInstanceData, 0);
    arrayResize(_sphereInstanceData, 0);
    _camera->draw(_drawables);

    _shader.setProjectionMatrix(_camera->projectionMatrix());

    /* Upload instance data to the GPU (orphaning the previous buffer
        contents) and draw all cubes in one call, and all spheres (if any)
        in another call */
    _boxInstanceBuffer.setData(_boxInstanceData, GL::BufferUsage::DynamicDraw);
    _box.setInstanceCount(_boxInstanceData.size());
    _shader.draw(_box);

    _sphereInstanceBuffer.setData(_sphereInstanceData, GL::BufferUsage::DynamicDraw);
    _sphere.setInstanceCount(_sphereInstanceData.size());
    _shader.draw(_sphere);

    swapBuffers();
    _timeline.nextFrame();
    redraw();
}

void PhysXExample::keyPressEvent(KeyEvent& event) {
    /* Movement */
    if(event.key() == KeyEvent::Key::Down) {
        _cameraObject->rotateX(5.0_degf);
    } else if(event.key() == KeyEvent::Key::Up) {
        _cameraObject->rotateX(-5.0_degf);
    } else if(event.key() == KeyEvent::Key::Left) {
        _cameraRig->rotateY(-5.0_degf);
    } else if(event.key() == KeyEvent::Key::Right) {
        _cameraRig->rotateY(5.0_degf);

    /* What to shoot */
    } else if(event.key() == KeyEvent::Key::S) {
        _shootBox ^= true;
    } else return;

    event.setAccepted();
}

void PhysXExample::mousePressEvent(MouseEvent& event) {
    /* Shoot an object on click */
    if(event.button() == MouseEvent::Button::Left) {
        /* First scale the position from being relative to window size to being
           relative to framebuffer size as those two can be different on HiDPI
           systems */
        const Vector2i position = event.position()*Vector2{framebufferSize()}/Vector2{windowSize()};
        const Vector2 clickPoint = Vector2::yScale(-1.0f)*(Vector2{position}/Vector2{framebufferSize()} - Vector2{0.5f})*_camera->projectionSize();
        const Vector3 direction = (_cameraObject->absoluteTransformation().rotationScaling()*Vector3{clickPoint, -1.0f}).normalized();

        const Float halfExtent = 0.5f;
        PxShape* shape = _shootBox ?
            _pxPhysics->createShape(PxBoxGeometry(halfExtent, halfExtent, halfExtent), *_pxMaterial) :
            _pxPhysics->createShape(PxSphereGeometry(halfExtent), *_pxMaterial);
        auto* object = new RigidBody{
            &_scene, _shootBox ? 1.0f : 5.0f, *_pxPhysics, *_pxScene, *shape,
            Matrix4::translation(_cameraObject->absoluteTransformation().translation())};

        /* Create either a box or a sphere */
        new ColoredDrawable{*object,
            _shootBox ? _boxInstanceData : _sphereInstanceData,
            _shootBox ? 0x880000_rgbf : 0x220000_rgbf,
            Matrix4::scaling(Vector3{_shootBox ? 0.5f : 0.25f}), _drawables};

        /* Give it an initial velocity */
        Vector3 d = direction*50.0f;
        object->rigidBody().setLinearVelocity(PxVec3{d.x(), d.y(), d.z()});

        event.setAccepted();
    }
}

}}

MAGNUM_APPLICATION_MAIN(Magnum::Examples::PhysXExample)
