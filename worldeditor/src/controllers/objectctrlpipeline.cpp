#include "objectctrlpipeline.h"

#include "rendertexture.h"
#include "commandbuffer.h"

#include "components/camera.h"
#include "components/actor.h"
#include "components/transform.h"
#include "components/scene.h"

#include "components/renderable.h"

#include "resources/material.h"
#include "resources/mesh.h"

#include "postprocess/postprocessor.h"

#include "settingsmanager.h"

#include "objectctrl.h"

#include <handles/handletools.h>

#include <QVariant>
#include <QColor>

#define SELECT_MAP  "selectMap"
#define DEPTH_MAP   "depthMap"
#define OUTLINE_MAP "outlineMap"
#define OUTDEPTH_MAP "outdepthMap"

#define G_EMISSIVE  "emissiveMap"

#define OUTLINE     "Outline"

class Outline : public PostProcessor {
public:
    Outline() :
        m_Width(1.0f) {
        Material *material = Engine::loadResource<Material>(".embedded/outline.mtl");
        if(material) {
            m_pMaterial = material->createInstance();
            m_pMaterial->setFloat("width", &m_Width);
            m_pMaterial->setVector4("color", &m_Color);
        }

        m_pResultTexture = Engine::objectCreate<RenderTexture>();
        m_pResultTexture->setTarget(Texture::RGBA8);

        setEnabled(true);
    }

    float m_Width;
    Vector4 m_Color;
};

ObjectCtrlPipeline::ObjectCtrlPipeline() :
        Pipeline() {

    RenderTexture *select = Engine::objectCreate<RenderTexture>();
    select->setTarget(Texture::RGBA8);
    m_Targets[SELECT_MAP] = select;
    m_Buffer->setGlobalTexture(SELECT_MAP, select);

    RenderTexture *depth = Engine::objectCreate<RenderTexture>();
    depth->setDepth(24);
    m_Targets[OUTDEPTH_MAP] = depth;
    m_Buffer->setGlobalTexture(OUTDEPTH_MAP, depth);

    RenderTexture *outline = Engine::objectCreate<RenderTexture>();
    outline->setTarget(Texture::RGBA8);
    m_Targets[OUTLINE_MAP] = outline;
    m_Buffer->setGlobalTexture(OUTLINE_MAP, outline);

    m_pGrid  = Engine::objectCreate<Mesh>("Grid");

    m_PostEffects[OUTLINE] = new Outline();

    Mesh::Lod lod;
    lod.vertices.resize(404);
    lod.indices.resize(404);
    for(uint8_t x = 0; x <= 100; x++) {
        uint32_t index = x * 2;
        lod.vertices[index] = Vector3(x - 50, -50, 0);
        lod.vertices[index + 1] = Vector3(x - 50, 50, 0);

        lod.indices[index] = index;
        lod.indices[index + 1] = index + 1;
    }
    for(uint8_t y = 0; y <= 100; y++) {
        uint32_t index = y * 2 + 202;
        lod.vertices[index] = Vector3(-50, y - 50, 0);
        lod.vertices[index + 1] = Vector3(50, y - 50, 0);

        lod.indices[index] = index;
        lod.indices[index + 1] = index + 1;
    }

    m_pGrid->setMode(Mesh::MODE_LINES);
    m_pGrid->addLod(lod);

    Material *m = Engine::loadResource<Material>(".embedded/gizmo.mtl");
    if(m) {
        m_pGizmo = m->createInstance();
    }

    loadSettings();
}

void ObjectCtrlPipeline::loadSettings() {
    QColor color = SettingsManager::instance()->property("General/Colors/Grid_Color").value<QColor>();
    m_SecondaryGridColor = m_PrimaryGridColor = Vector4(color.redF(), color.greenF(), color.blueF(), color.alphaF());

    color = SettingsManager::instance()->property("General/Colors/Outline_Color").value<QColor>();
    Outline *outline = static_cast<Outline *>(m_PostEffects[OUTLINE]);
    outline->m_Color = Vector4(color.redF(), color.greenF(), color.blueF(), color.alphaF());
    outline->m_Width = SettingsManager::instance()->property("General/Colors/Outline_Width").toFloat();
}

void ObjectCtrlPipeline::setController(ObjectCtrl *ctrl) {
    m_pController = ctrl;
}

void ObjectCtrlPipeline::draw(Camera &camera) {
    // Retrive object id
    m_Buffer->setRenderTarget({m_Targets[SELECT_MAP]}, m_Targets[DEPTH_MAP]);
    m_Buffer->clearRenderTarget();

    m_Buffer->setViewport(0, 0, static_cast<int32_t>(m_Screen.x), static_cast<int32_t>(m_Screen.y));

    cameraReset(camera);
    drawComponents(ICommandBuffer::RAYCAST, m_Filter);

    Pipeline::draw(camera);

    // Draw handles
    cameraReset(camera);
    m_Buffer->setRenderTarget({m_pFinal}, m_Targets[DEPTH_MAP]);
    drawGrid(camera);

    Handles::beginDraw(m_Buffer);
    m_pController->drawHandles();
    Handles::endDraw();
}

void ObjectCtrlPipeline::post(Camera &camera) {
    cameraReset(camera);
    // Selection outline
    m_Buffer->setRenderTarget({m_Targets[OUTLINE_MAP]}, m_Targets[OUTDEPTH_MAP]);
    m_Buffer->clearRenderTarget();
    ObjectList filter;
    for(auto actor : m_pController->selected()) {
        for(auto it : m_Filter) {
            Component *component = dynamic_cast<Component *>(it);
            if(component && component->actor() == actor) {
                filter.push_back(component);
            }
        }
    }
    drawComponents(ICommandBuffer::RAYCAST, filter);

    // Step4 - Post Processing passes
    m_Buffer->setScreenProjection();
    m_pFinal = postProcess(m_Targets[G_EMISSIVE], {OUTLINE, "AntiAliasing", "Bloom"});
}

void ObjectCtrlPipeline::resize(int32_t width, int32_t height) {
    Pipeline::resize(width, height);
    m_pController->resize(width, height);
}

void ObjectCtrlPipeline::drawGrid(Camera &camera) {
    Transform *t = camera.actor()->transform();
    Vector3 cam = t->position();
    Vector3 pos;
    float length;

    if(camera.orthographic()) {
        pos = Vector3(cam.x, cam.y, 0.0);
        length = camera.orthoSize();
    } else {
        pos = Vector3(cam.x, 0.0f, cam.z);
        length = (pos - cam).length();
    }

    float scale = 0.001f;
    while(scale < length) {
        scale *= 10.0f;
    }
    m_SecondaryGridColor.w = m_PrimaryGridColor.w * (1.0f - (length / scale));

    Matrix4 transform(Vector3(scale * int32_t(pos.x / scale), 0.0f, scale * int32_t(pos.z / scale)),
                     (camera.orthographic()) ? Quaternion() : Quaternion(Vector3(1, 0, 0), 90.0f), scale);

    m_Buffer->setColor(m_PrimaryGridColor);
    m_Buffer->drawMesh(transform, m_pGrid, ICommandBuffer::TRANSLUCENT, m_pGizmo);

    Matrix4 m;
    m.scale(0.1f);

    m_Buffer->setColor(m_SecondaryGridColor);
    m_Buffer->drawMesh(transform * m, m_pGrid, ICommandBuffer::TRANSLUCENT, m_pGizmo);

    m_Buffer->setColor(Vector4(1.0f));
}
