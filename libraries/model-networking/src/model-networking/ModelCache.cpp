//
//  ModelCache.cpp
//  libraries/model-networking
//
//  Created by Zach Pomerantz on 3/15/16.
//  Copyright 2016 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "ModelCache.h"
#include <FSTReader.h>
#include "FBXReader.h"
#include "OBJReader.h"

#include <gpu/Batch.h>
#include <gpu/Stream.h>

#include <QThreadPool>

#include "ModelNetworkingLogging.h"

class GeometryReader;

class GeometryExtra {
public:
    const QVariantHash& mapping;
    const QUrl& textureBaseUrl;
};

class GeometryMappingResource : public GeometryResource {
    Q_OBJECT
public:
    GeometryMappingResource(const QUrl& url) : GeometryResource(url) {};

    virtual void downloadFinished(const QByteArray& data) override;

private slots:
    void onGeometryMappingLoaded(bool success);

private:
    GeometryResource::Pointer _geometryResource;
};

void GeometryMappingResource::downloadFinished(const QByteArray& data) {
    auto mapping = FSTReader::readMapping(data);

    QString filename = mapping.value("filename").toString();
    if (filename.isNull()) {
        qCDebug(modelnetworking) << "Mapping file" << _url << "has no \"filename\" field";
        finishedLoading(false);
    } else {
        QUrl url = _url.resolved(filename);
        QUrl textureBaseUrl;

        QString texdir = mapping.value("texdir").toString();
        if (!texdir.isNull()) {
            if (!texdir.endsWith('/')) {
                texdir += '/';
            }
            textureBaseUrl = _url.resolved(texdir);
        }

        auto modelCache = DependencyManager::get<ModelCache>();
        GeometryExtra extra{ mapping, textureBaseUrl };

        // Get the raw GeometryResource, not the wrapped NetworkGeometry
        _geometryResource = modelCache->getResource(url, QUrl(), true, &extra).staticCast<GeometryResource>();

        if (_geometryResource->isLoaded()) {
            onGeometryMappingLoaded(!_geometryResource->getURL().isEmpty());
        } else {
            connect(_geometryResource.data(), &Resource::finished, this, &GeometryMappingResource::onGeometryMappingLoaded);
        }

        // Avoid caching nested resources - their references will be held by the parent
        _geometryResource->_isCacheable = false;
    }
}

void GeometryMappingResource::onGeometryMappingLoaded(bool success) {
    if (success) {
        _geometry = _geometryResource->_geometry;
        _shapes = _geometryResource->_shapes;
        _meshes = _geometryResource->_meshes;
        _materials = _geometryResource->_materials;
    }
    finishedLoading(success);
}

class GeometryReader : public QRunnable {
public:
    GeometryReader(QWeakPointer<Resource>& resource, const QUrl& url, const QVariantHash& mapping,
        const QByteArray& data) :
        _resource(resource), _url(url), _mapping(mapping), _data(data) {}
    virtual ~GeometryReader() = default;

    virtual void run() override;

private:
    QWeakPointer<Resource> _resource;
    QUrl _url;
    QVariantHash _mapping;
    QByteArray _data;
};

void GeometryReader::run() {
    auto originalPriority = QThread::currentThread()->priority();
    if (originalPriority == QThread::InheritPriority) {
        originalPriority = QThread::NormalPriority;
    }
    QThread::currentThread()->setPriority(QThread::LowPriority);

    // Ensure the resource is still being requested
    auto resource = _resource.toStrongRef();
    if (!resource) {
        qCWarning(modelnetworking) << "Abandoning load of" << _url << "; could not get strong ref";
        return;
    }

    try {
        if (_data.isEmpty()) {
            throw QString("reply is NULL");
        }

        QString urlname = _url.path().toLower();
        if (!urlname.isEmpty() && !_url.path().isEmpty() &&
            (_url.path().toLower().endsWith(".fbx") || _url.path().toLower().endsWith(".obj"))) {
            FBXGeometry* fbxGeometry = nullptr;

            if (_url.path().toLower().endsWith(".fbx")) {
                fbxGeometry = readFBX(_data, _mapping, _url.path());
                if (fbxGeometry->meshes.size() == 0 && fbxGeometry->joints.size() == 0) {
                    throw QString("empty geometry, possibly due to an unsupported FBX version");
                }
            } else if (_url.path().toLower().endsWith(".obj")) {
                fbxGeometry = OBJReader().readOBJ(_data, _mapping, _url);
            } else {
                throw QString("unsupported format");
            }

            QMetaObject::invokeMethod(resource.data(), "setGeometryDefinition",
                Q_ARG(void*, fbxGeometry));
        } else {
            throw QString("url is invalid");
        }
    } catch (const QString& error) {
        qCDebug(modelnetworking) << "Error reading " << _url << ": " << error;
        QMetaObject::invokeMethod(resource.data(), "finishedLoading", Q_ARG(bool, false));
    }

    QThread::currentThread()->setPriority(originalPriority);
}

class GeometryDefinitionResource : public GeometryResource {
    Q_OBJECT
public:
    GeometryDefinitionResource(const QUrl& url, const QVariantHash& mapping, const QUrl& textureBaseUrl) :
        GeometryResource(url), _mapping(mapping), _textureBaseUrl(textureBaseUrl.isValid() ? textureBaseUrl : url) {}

    virtual void downloadFinished(const QByteArray& data) override;

protected:
    Q_INVOKABLE void setGeometryDefinition(void* fbxGeometry);

private:
    QVariantHash _mapping;
    QUrl _textureBaseUrl;
};

void GeometryDefinitionResource::downloadFinished(const QByteArray& data) {
    QThreadPool::globalInstance()->start(new GeometryReader(_self, _url, _mapping, data));
}

void GeometryDefinitionResource::setGeometryDefinition(void* fbxGeometry) {
    // Assume ownership of the geometry pointer
    _geometry.reset(static_cast<FBXGeometry*>(fbxGeometry));

    // Copy materials
    QHash<QString, size_t> materialIDAtlas;
    for (const FBXMaterial& material : _geometry->materials) {
        materialIDAtlas[material.materialID] = _materials.size();
        _materials.push_back(std::make_shared<NetworkMaterial>(material, _textureBaseUrl));
    }

    std::shared_ptr<NetworkMeshes> meshes = std::make_shared<NetworkMeshes>();
    std::shared_ptr<NetworkShapes> shapes = std::make_shared<NetworkShapes>();
    int meshID = 0;
    for (const FBXMesh& mesh : _geometry->meshes) {
        // Copy mesh pointers
        meshes->emplace_back(mesh._mesh);
        int partID = 0;
        for (const FBXMeshPart& part : mesh.parts) {
            // Construct local shapes
            shapes->push_back(std::make_shared<NetworkShape>(meshID, partID, (int)materialIDAtlas[part.materialID]));
            partID++;
        }
        meshID++;
    }
    _meshes = meshes;
    _shapes = shapes;

    finishedLoading(true);
}

ModelCache::ModelCache() {
    const qint64 GEOMETRY_DEFAULT_UNUSED_MAX_SIZE = DEFAULT_UNUSED_MAX_SIZE;
    setUnusedResourceCacheSize(GEOMETRY_DEFAULT_UNUSED_MAX_SIZE);
}

QSharedPointer<Resource> ModelCache::createResource(const QUrl& url, const QSharedPointer<Resource>& fallback,
                                                    bool delayLoad, const void* extra) {
    const GeometryExtra* geometryExtra = static_cast<const GeometryExtra*>(extra);

    Resource* resource = nullptr;
    if (url.path().toLower().endsWith(".fst")) {
        resource = new GeometryMappingResource(url);
    } else {
        resource = new GeometryDefinitionResource(url, geometryExtra->mapping, geometryExtra->textureBaseUrl);
    }

    return QSharedPointer<Resource>(resource, &Resource::allReferencesCleared);
}

std::shared_ptr<NetworkGeometry> ModelCache::getGeometry(const QUrl& url, const QVariantHash& mapping, const QUrl& textureBaseUrl) {
    GeometryExtra geometryExtra = { mapping, textureBaseUrl };
    GeometryResource::Pointer resource = getResource(url, QUrl(), true, &geometryExtra).staticCast<GeometryResource>();
    return std::make_shared<NetworkGeometry>(resource);
}

const QVariantMap Geometry::getTextures() const {
    QVariantMap textures;
    for (const auto& material : _materials) {
        for (const auto& texture : material->_textures) {
            if (texture.texture) {
                textures[texture.name] = texture.texture->getURL();
            }
        }
    }

    return textures;
}

// FIXME: The materials should only be copied when modified, but the Model currently caches the original
Geometry::Geometry(const Geometry& geometry) {
    _geometry = geometry._geometry;
    _meshes = geometry._meshes;
    _shapes = geometry._shapes;

    _materials.reserve(geometry._materials.size());
    for (const auto& material : geometry._materials) {
        _materials.push_back(std::make_shared<NetworkMaterial>(*material));
    }
}

void Geometry::setTextures(const QVariantMap& textureMap) {
    if (_meshes->size() > 0) {
        for (auto& material : _materials) {
            // Check if any material textures actually changed
            if (std::any_of(material->_textures.cbegin(), material->_textures.cend(),
                [&textureMap](const NetworkMaterial::Textures::value_type& it) { return it.texture && textureMap.contains(it.name); })) { 

                // FIXME: The Model currently caches the materials (waste of space!)
                //        so they must be copied in the Geometry copy-ctor
                // if (material->isOriginal()) {
                //    // Copy the material to avoid mutating the cached version
                //    material = std::make_shared<NetworkMaterial>(*material);
                //}

                material->setTextures(textureMap);
                _areTexturesLoaded = false;
            }
        }
    } else {
        qCWarning(modelnetworking) << "Ignoring setTextures(); geometry not ready";
    }
}

bool Geometry::areTexturesLoaded() const {
    if (!_areTexturesLoaded) {
        _hasTransparentTextures = false;

        for (auto& material : _materials) {
            // Check if material textures are loaded
            if (std::any_of(material->_textures.cbegin(), material->_textures.cend(),
                [](const NetworkMaterial::Textures::value_type& it) { return it.texture && !it.texture->isLoaded(); })) {

                return false;
            }

            // If material textures are loaded, check the material translucency
            const auto albedoTexture = material->_textures[NetworkMaterial::MapChannel::ALBEDO_MAP];
            if (albedoTexture.texture && albedoTexture.texture->getGPUTexture()) {
                material->resetOpacityMap();

                _hasTransparentTextures |= material->getKey().isTranslucent();
            }
        }

        _areTexturesLoaded = true;
    }
    return true;
}

const std::shared_ptr<const NetworkMaterial> Geometry::getShapeMaterial(int shapeID) const {
    if ((shapeID >= 0) && (shapeID < (int)_shapes->size())) {
        int materialID = _shapes->at(shapeID)->materialID;
        if ((materialID >= 0) && (materialID < (int)_materials.size())) {
            return _materials[materialID];
        }
    }
    return nullptr;
}

NetworkGeometry::NetworkGeometry(const GeometryResource::Pointer& networkGeometry) : _resource(networkGeometry) {
    connect(_resource.data(), &Resource::finished, this, &NetworkGeometry::resourceFinished);
    connect(_resource.data(), &Resource::onRefresh, this, &NetworkGeometry::resourceRefreshed);
    if (_resource->isLoaded()) {
        resourceFinished(!_resource->getURL().isEmpty());
    }
}

void NetworkGeometry::resourceFinished(bool success) {
    // FIXME: Model is not set up to handle a refresh
    if (_instance) {
        return;
    }
    if (success) {
        _instance = std::make_shared<Geometry>(*_resource);
    }
    emit finished(success);
}

void NetworkGeometry::resourceRefreshed() {
    // FIXME: Model is not set up to handle a refresh
    // _instance.reset();
}

const QString NetworkMaterial::NO_TEXTURE = QString();

const QString& NetworkMaterial::getTextureName(MapChannel channel) {
    if (_textures[channel].texture) {
        return _textures[channel].name;
    }
    return NO_TEXTURE;
}

QUrl NetworkMaterial::getTextureUrl(const QUrl& url, const FBXTexture& texture) {
    // If content is inline, cache it under the fbx file, not its url
    const auto baseUrl = texture.content.isEmpty() ? url : QUrl(url.url() + "/");
    return baseUrl.resolved(QUrl(texture.filename));
}

model::TextureMapPointer NetworkMaterial::fetchTextureMap(const QUrl& baseUrl, const FBXTexture& fbxTexture,
                                                        TextureType type, MapChannel channel) {
    const auto url = getTextureUrl(baseUrl, fbxTexture);
    const auto texture = DependencyManager::get<TextureCache>()->getTexture(url, type, fbxTexture.content);
    _textures[channel] = Texture { fbxTexture.name, texture };

    auto map = std::make_shared<model::TextureMap>();
    map->setTextureSource(texture->_textureSource);
    return map;
}

model::TextureMapPointer NetworkMaterial::fetchTextureMap(const QUrl& url, TextureType type, MapChannel channel) {
    const auto texture = DependencyManager::get<TextureCache>()->getTexture(url, type);
    _textures[channel].texture = texture;

    auto map = std::make_shared<model::TextureMap>();
    map->setTextureSource(texture->_textureSource);
    return map;
}

NetworkMaterial::NetworkMaterial(const FBXMaterial& material, const QUrl& textureBaseUrl) :
    model::Material(*material._material)
{
    _textures = Textures(MapChannel::NUM_MAP_CHANNELS);
    if (!material.albedoTexture.filename.isEmpty()) {
        auto map = fetchTextureMap(textureBaseUrl, material.albedoTexture, DEFAULT_TEXTURE, MapChannel::ALBEDO_MAP);
        _albedoTransform = material.albedoTexture.transform;
        map->setTextureTransform(_albedoTransform);

        if (!material.opacityTexture.filename.isEmpty()) {
            if (material.albedoTexture.filename == material.opacityTexture.filename) {
                // Best case scenario, just indicating that the albedo map contains transparency
                // TODO: Different albedo/opacity maps are not currently supported
                map->setUseAlphaChannel(true);
            }
        }

        setTextureMap(MapChannel::ALBEDO_MAP, map);
    }


    if (!material.normalTexture.filename.isEmpty()) {
        auto type = (material.normalTexture.isBumpmap ? BUMP_TEXTURE : NORMAL_TEXTURE);
        auto map = fetchTextureMap(textureBaseUrl, material.normalTexture, type, MapChannel::NORMAL_MAP);
        setTextureMap(MapChannel::NORMAL_MAP, map);
    }

    if (!material.roughnessTexture.filename.isEmpty()) {
        auto map = fetchTextureMap(textureBaseUrl, material.roughnessTexture, ROUGHNESS_TEXTURE, MapChannel::ROUGHNESS_MAP);
        setTextureMap(MapChannel::ROUGHNESS_MAP, map);
    } else if (!material.glossTexture.filename.isEmpty()) {
        auto map = fetchTextureMap(textureBaseUrl, material.glossTexture, GLOSS_TEXTURE, MapChannel::ROUGHNESS_MAP);
        setTextureMap(MapChannel::ROUGHNESS_MAP, map);
    }

    if (!material.metallicTexture.filename.isEmpty()) {
        auto map = fetchTextureMap(textureBaseUrl, material.metallicTexture, METALLIC_TEXTURE, MapChannel::METALLIC_MAP);
        setTextureMap(MapChannel::METALLIC_MAP, map);
    } else if (!material.specularTexture.filename.isEmpty()) {
        auto map = fetchTextureMap(textureBaseUrl, material.specularTexture, SPECULAR_TEXTURE, MapChannel::METALLIC_MAP);
        setTextureMap(MapChannel::METALLIC_MAP, map);
    }

    if (!material.occlusionTexture.filename.isEmpty()) {
        auto map = fetchTextureMap(textureBaseUrl, material.occlusionTexture, OCCLUSION_TEXTURE, MapChannel::OCCLUSION_MAP);
        setTextureMap(MapChannel::OCCLUSION_MAP, map);
    }

    if (!material.emissiveTexture.filename.isEmpty()) {
        auto map = fetchTextureMap(textureBaseUrl, material.emissiveTexture, EMISSIVE_TEXTURE, MapChannel::EMISSIVE_MAP);
        setTextureMap(MapChannel::EMISSIVE_MAP, map);
    }

    if (!material.lightmapTexture.filename.isEmpty()) {
        auto map = fetchTextureMap(textureBaseUrl, material.lightmapTexture, LIGHTMAP_TEXTURE, MapChannel::LIGHTMAP_MAP);
        _lightmapTransform = material.lightmapTexture.transform;
        _lightmapParams = material.lightmapParams;
        map->setTextureTransform(_lightmapTransform);
        map->setLightmapOffsetScale(_lightmapParams.x, _lightmapParams.y);
        setTextureMap(MapChannel::LIGHTMAP_MAP, map);
    }
}

void NetworkMaterial::setTextures(const QVariantMap& textureMap) {
    _isOriginal = false;

    const auto& albedoName = getTextureName(MapChannel::ALBEDO_MAP);
    const auto& normalName = getTextureName(MapChannel::NORMAL_MAP);
    const auto& roughnessName = getTextureName(MapChannel::ROUGHNESS_MAP);
    const auto& metallicName = getTextureName(MapChannel::METALLIC_MAP);
    const auto& occlusionName = getTextureName(MapChannel::OCCLUSION_MAP);
    const auto& emissiveName = getTextureName(MapChannel::EMISSIVE_MAP);
    const auto& lightmapName = getTextureName(MapChannel::LIGHTMAP_MAP);

    if (!albedoName.isEmpty()) {
        auto url = textureMap.contains(albedoName) ? textureMap[albedoName].toUrl() : QUrl();
        auto map = fetchTextureMap(url, DEFAULT_TEXTURE, MapChannel::ALBEDO_MAP);
        map->setTextureTransform(_albedoTransform);
        // when reassigning the albedo texture we also check for the alpha channel used as opacity
        map->setUseAlphaChannel(true);
        setTextureMap(MapChannel::ALBEDO_MAP, map);
    }

    if (!normalName.isEmpty()) {
        auto url = textureMap.contains(normalName) ? textureMap[normalName].toUrl() : QUrl();
        auto map = fetchTextureMap(url, DEFAULT_TEXTURE, MapChannel::NORMAL_MAP);
        setTextureMap(MapChannel::NORMAL_MAP, map);
    }

    if (!roughnessName.isEmpty()) {
        auto url = textureMap.contains(roughnessName) ? textureMap[roughnessName].toUrl() : QUrl();
        // FIXME: If passing a gloss map instead of a roughmap how do we know?
        auto map = fetchTextureMap(url, ROUGHNESS_TEXTURE, MapChannel::ROUGHNESS_MAP);
        setTextureMap(MapChannel::ROUGHNESS_MAP, map);
    }

    if (!metallicName.isEmpty()) {
        auto url = textureMap.contains(metallicName) ? textureMap[metallicName].toUrl() : QUrl();
        // FIXME: If passing a specular map instead of a metallic how do we know?
        auto map = fetchTextureMap(url, METALLIC_TEXTURE, MapChannel::METALLIC_MAP);
        setTextureMap(MapChannel::METALLIC_MAP, map);
    }

    if (!occlusionName.isEmpty()) {
        auto url = textureMap.contains(occlusionName) ? textureMap[occlusionName].toUrl() : QUrl();
        auto map = fetchTextureMap(url, OCCLUSION_TEXTURE, MapChannel::OCCLUSION_MAP);
        setTextureMap(MapChannel::OCCLUSION_MAP, map);
    }

    if (!emissiveName.isEmpty()) {
        auto url = textureMap.contains(emissiveName) ? textureMap[emissiveName].toUrl() : QUrl();
        auto map = fetchTextureMap(url, EMISSIVE_TEXTURE, MapChannel::EMISSIVE_MAP);
        setTextureMap(MapChannel::EMISSIVE_MAP, map);
    }

    if (!lightmapName.isEmpty()) {
        auto url = textureMap.contains(lightmapName) ? textureMap[lightmapName].toUrl() : QUrl();
        auto map = fetchTextureMap(url, LIGHTMAP_TEXTURE, MapChannel::LIGHTMAP_MAP);
        map->setTextureTransform(_lightmapTransform);
        map->setLightmapOffsetScale(_lightmapParams.x, _lightmapParams.y);
        setTextureMap(MapChannel::LIGHTMAP_MAP, map);
    }
}

#include "ModelCache.moc"
