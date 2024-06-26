#include <filesystem>
#include <sstream>
#include <fstream>
#include "AssetPack.h"
#include "Log.h"

// import
#include "../Psd/Psd.h"
#include "../Psd/PsdMallocAllocator.h"
#include "../Psd/PsdLayerMaskSection.h"
#include "../Psd/PsdNativeFile.h"
#include "../Psd/PsdDocument.h"
#include "../Psd/PsdParseDocument.h"
#include "../Psd/PsdColorMode.h"
#include "../Psd/PsdLayer.h"
#include "../Psd/PsdParseLayerMaskSection.h"
#include "../Psd/PsdLayerCanvasCopy.h"
#include "../Psd/PsdChannel.h"
#include "../Psd/PsdInterleave.h"
#include "../Psd/PsdChannelType.h"
#include "../Psd/PsdLayerType.h"
#include "../Psd/PsdBlendMode.h"

// export
#include "Utils.h"
#include "stb_image_resize.h"
#include "stb_image_write.h"
#include "json/json.h"

// helpers for reading PSDs
namespace
{
PSD_USING_NAMESPACE;
static const unsigned int CHANNEL_NOT_FOUND = UINT_MAX;


// ---------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------
template <typename T, typename DataHolder>
static void* ExpandChannelToCanvas(Allocator* allocator, const DataHolder* layer, const void* data, unsigned int canvasWidth, unsigned int canvasHeight)
{
	T* canvasData = static_cast<T*>(allocator->Allocate(sizeof(T)*canvasWidth*canvasHeight, 16u));
	memset(canvasData, 0u, sizeof(T)*canvasWidth*canvasHeight);

	imageUtil::CopyLayerData(static_cast<const T*>(data), canvasData, layer->left, layer->top, layer->right, layer->bottom, canvasWidth, canvasHeight);

	return canvasData;
}


// ---------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------
static void* ExpandChannelToCanvas(const Document* document, Allocator* allocator, Layer* layer, Channel* channel)
{
	if (document->bitsPerChannel == 8)
		return ExpandChannelToCanvas<uint8_t>(allocator, layer, channel->data, document->width, document->height);
	else if (document->bitsPerChannel == 16)
		return ExpandChannelToCanvas<uint16_t>(allocator, layer, channel->data, document->width, document->height);
	else if (document->bitsPerChannel == 32)
		return ExpandChannelToCanvas<float32_t>(allocator, layer, channel->data, document->width, document->height);

	return nullptr;
}


// ---------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------
template <typename T>
static void* ExpandMaskToCanvas(const Document* document, Allocator* allocator, T* mask)
{
	if (document->bitsPerChannel == 8)
		return ExpandChannelToCanvas<uint8_t>(allocator, mask, mask->data, document->width, document->height);
	else if (document->bitsPerChannel == 16)
		return ExpandChannelToCanvas<uint16_t>(allocator, mask, mask->data, document->width, document->height);
	else if (document->bitsPerChannel == 32)
		return ExpandChannelToCanvas<float32_t>(allocator, mask, mask->data, document->width, document->height);

	return nullptr;
}


// ---------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------
unsigned int FindChannel(Layer* layer, int16_t channelType)
{
	for (unsigned int i = 0; i < layer->channelCount; ++i)
	{
		Channel* channel = &layer->channels[i];
		if (channel->data && channel->type == channelType)
			return i;
	}

	return CHANNEL_NOT_FOUND;
}


// ---------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------
template <typename T>
T* CreateInterleavedImage(Allocator* allocator, const void* srcR, const void* srcG, const void* srcB, unsigned int width, unsigned int height)
{
	T* image = static_cast<T*>(allocator->Allocate(width*height * 4u*sizeof(T), 16u));

	const T* r = static_cast<const T*>(srcR);
	const T* g = static_cast<const T*>(srcG);
	const T* b = static_cast<const T*>(srcB);
	imageUtil::InterleaveRGB(r, g, b, T(0), image, width, height);

	return image;
}


// ---------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------
template <typename T>
T* CreateInterleavedImage(Allocator* allocator, const void* srcR, const void* srcG, const void* srcB, const void* srcA, unsigned int width, unsigned int height)
{
	T* image = static_cast<T*>(allocator->Allocate(width*height * 4u*sizeof(T), 16u));

	const T* r = static_cast<const T*>(srcR);
	const T* g = static_cast<const T*>(srcG);
	const T* b = static_cast<const T*>(srcB);
	const T* a = static_cast<const T*>(srcA);
	imageUtil::InterleaveRGBA(r, g, b, a, image, width, height);

	return image;
}

// a few more helpers

enum PositionParseStatus {
	NotParsed,
	ParsedSizeOnly,
	ParseDone,
};

uint8_t* GetLayerData(const Document* document, File* file, Allocator &allocator, Layer* layer) {
	const unsigned int indexR = FindChannel(layer, channelType::R);
	const unsigned int indexG = FindChannel(layer, channelType::G);
	const unsigned int indexB = FindChannel(layer, channelType::B);
	const unsigned int indexA = FindChannel(layer, channelType::TRANSPARENCY_MASK);
	const bool allChannelsFound =
		indexR!=CHANNEL_NOT_FOUND &&
		indexG!=CHANNEL_NOT_FOUND &&
		indexB!=CHANNEL_NOT_FOUND &&
		indexA!=CHANNEL_NOT_FOUND;

	if (!allChannelsFound) {
		ERR("some layer channels were not found!")
		return nullptr;
	}

	void* canvasData[4] = {
		ExpandChannelToCanvas(document, &allocator, layer, &layer->channels[indexR]),
		ExpandChannelToCanvas(document, &allocator, layer, &layer->channels[indexG]),
		ExpandChannelToCanvas(document, &allocator, layer, &layer->channels[indexB]),
		ExpandChannelToCanvas(document, &allocator, layer, &layer->channels[indexA]),
	};
	uint8_t* layerData = CreateInterleavedImage<uint8_t>(
		&allocator, canvasData[0], canvasData[1], canvasData[2], canvasData[3], document->width, document->height);
	allocator.Free(canvasData[0]);
	allocator.Free(canvasData[1]);
	allocator.Free(canvasData[2]);
	allocator.Free(canvasData[3]);
	return layerData;
}
}

bool AssetPack::isValid() const {
	bool valid = true;
	for (auto& spritePair : spriteSets) {
		auto& sprite = spritePair.second;
		if (sprite.baseLayersData.empty()) {
			std::string baseName = sprite.getBaseName();
			if (baseName.empty()) baseName = "(unknown)";
			AppendToGUILog({LT_ERROR, "ERROR: sprite '" + baseName + "' doesn't have a base layer!"});
			valid = false;
		}
	}
	return valid;
}

bool EchoesReadPsdToAssetPack(const std::string& inFile, AssetPack& assetPack) {

	psd::Document* document;
	psd::LayerMaskSection* section;
	psd::MallocAllocator allocator;
	psd::NativeFile file(&allocator);
	if (!ReadPsdLayers(inFile, document, section, allocator, file)) {
		return false;
	}

	////////////////////////////////////////////////////////////////
	// start building the asset pack

	// metadata
	assetPack.docWidth = document->width;
	assetPack.docHeight = document->height;

	// layers first pass
	for (int i = 0; i < section->layerCount; i++) {
		psd::Layer* layer = &section->layers[i];
		// info layers:
		if (layer->parent != nullptr && tolower(GetName(layer->parent))=="meta") {
			if (tolower(GetName(layer)) == "origin") {
				assetPack.docOriginPx = {
					(layer->right + layer->left) * 0.5f,
					(layer->bottom + layer->top) * 0.5f
				};
			} else {
				auto tokens = SplitTokens(GetName(layer));
				if (tokens.size() == 2 && tolower(tokens[0]) == "ruler") {
					assetPack.pixelsPerDiagonalUnit = (layer->right - layer->left) / std::stof(tokens[1]);
				}
			}
		}
		// export layers: just create something empty and insert into dict for now
		else if (
			VisibleInHierarchy(layer) &&
			(layer->type == layerType::OPEN_FOLDER || layer->type == layerType::CLOSED_FOLDER) &&
			layer->parent != nullptr &&
			tolower(GetName(layer->parent))=="export")
		{
			auto name = GetName(layer);
			assetPack.spriteSets[name] = SpriteSet();
			if (layer->layerMask) {
				WARN("folder for sprite '%s' has a layer mask, which will be ignored during export..", name.c_str())
				AppendToGUILog({LT_WARNING, "folder for sprite '" + name + "' has a layer mask, which will be ignored during export.."});
			}
		}
	}

	// layers second pass: actually parse data
	uint32_t layerDataSize = document->width * document->height * 4;

	const Layer* currentSpriteDivider = nullptr;
	const Layer* currentSpriteFolder = nullptr;
	const Layer* prevLayer = nullptr;
	SpriteSet* currentSprite = nullptr;
	std::string currentSpriteName;
	PositionParseStatus positionParseStatus = ParseDone;
	auto ProcessSpriteMetaData = [&](Layer* layer) {
		if (layer->layerMask) {
			WARN("base layer(s) of '%s' has a layer mask, which will be ignored during export..", currentSpriteName.c_str())
			AppendToGUILog({LT_WARNING, "base layer(s) of '" + currentSpriteName + "' has a layer mask, which will be ignored during export.."});
		}
		if (blendMode::KeyToEnum(layer->blendModeKey) != blendMode::NORMAL) {
			WARN("base layer of '%s' doesn't have normal blend mode- result might look different.", currentSpriteName.c_str())
			AppendToGUILog({LT_WARNING, "base layer of '" + currentSpriteName + "' doesn't have normal blend mode- result might look different."});
		}
		// name
		currentSprite->name = currentSpriteName;

		// unit dims
		auto tokens = SplitTokens(GetName(layer));
		try {
			if (tokens.size() == 4) {
				currentSprite->minUnit = {std::stof(tokens[0]), std::stof(tokens[1])};
				currentSprite->sizeUnit = {std::stof(tokens[2]), std::stof(tokens[3])};
				positionParseStatus = ParseDone;
			} else if (tokens.size() == 2) {
				currentSprite->sizeUnit = {std::stof(tokens[0]), std::stof(tokens[1])};
				positionParseStatus = ParsedSizeOnly;
			}
			return true;
		} catch(std::exception &e) {
			std::string msg = "Failed to parse position and size information for sprite '" + currentSprite->name + "'";
			AppendToGUILog({LT_WARNING, msg + ". Did you name the base layer with 2 or 4 numbers?"});
			WARN("%s: %s", msg.c_str(), e.what())
			return false;
		}
	};
	auto ExpandPixelBBox = [&](Layer* layer) {
		// pixel dims
		int minX = std::max(0, layer->left);
		int minY = std::max(0, layer->top);
		int maxX = std::min((int)document->width, layer->right);
		int maxY = std::min((int)document->height, layer->bottom);
		ivec2 minPx = {
			std::min(minX, currentSprite->minPx.x),
			std::min(minY, currentSprite->minPx.y)
		};
		ivec2 maxPx = {
			std::max(maxX, currentSprite->minPx.x+currentSprite->sizePx.x),
			std::max(maxY, currentSprite->minPx.y+currentSprite->sizePx.y)
		};
		currentSprite->minPx = minPx;
		currentSprite->sizePx = { maxPx.x - minPx.x, maxPx.y - minPx.y };
	};
	auto ProcessSpriteBaseOrLightLayerContent = [&](std::vector<std::vector<uint8_t>>& dst, Layer* layer) {
		ASSERT(layer->type == layerType::ANY)
		uint8_t* layerData = GetLayerData(document, &file, allocator, layer);
		if (!layerData) {
			file.Close(); return false;
		}
		dst.emplace_back();
		dst.back().resize(layerDataSize);
		memcpy(dst.back().data(), layerData, layerDataSize);
		allocator.Free(layerData);
		return true;
	};
	auto ProcessSpriteEmissionMaskContent = [&](std::vector<uint8_t>& dst, Layer* layer) {
		ASSERT(layer->type == layerType::ANY)
		uint8_t* layerData = GetLayerData(document, &file, allocator, layer);
		if (!layerData) {
			file.Close(); return false;
		}
		uint32_t numPx = document->width * document->height;
		// we only want the alpha channel
		dst.resize(numPx);
		for (uint32_t i = 0; i < numPx; i++) {
			dst[i] = layerData[i * 4 + 3];
		}
		allocator.Free(layerData);
		return true;
	};
	for (int i = 0; i < section->layerCount; i++) {
		Layer* layer = &section->layers[i];
		if (!IsOrUnderLayer(layer, "export") || (layer->type != layerType::SECTION_DIVIDER && !VisibleInHierarchy(layer))) continue;

		ExtractLayer(document, &file, &allocator, layer);

		// entering new sprite..
		if (layer->type == layerType::SECTION_DIVIDER &&
			layer->parent && // asset group
			layer->parent->parent && tolower(GetName(layer->parent->parent)) == "export" && // "export"
			assetPack.spriteSets.find(GetName(layer->parent)) != assetPack.spriteSets.end() // check there is a sprite set
			) {
			// check if last sprite has position info fully parsed, give warning if not:
			if (positionParseStatus != ParseDone) {
				if (positionParseStatus == NotParsed) {
					WARN("failed to parse position and size information for sprite %s; its pivot will be incorrect in Unity", currentSprite->name.c_str())
					AppendToGUILog({LT_WARNING, "failed to parse position and size for sprite '" + currentSprite->name + "'; its pivot will be incorrect in Unity. Refer to documentation for how to specify position and size information for sprites"});
				} else if (positionParseStatus == ParsedSizeOnly) {
					WARN("didn't find position information for sprite %s, its pivot will be incorrect in Unity: did you forget to include the 'corner' layer?", currentSprite->name.c_str())
					AppendToGUILog({LT_WARNING, "didn't find position information for sprite '" + currentSprite->name + "', its pivot will be incorrect in Unity: did you forget to include the 'corner' layer?"});
				}
				currentSprite->minUnit = {0, 0};
				currentSprite->sizeUnit = {1, 1};
			}
			// actually move on to new layer:
			currentSpriteDivider = layer;
			currentSpriteFolder = layer->parent;
			currentSpriteName = GetName(layer->parent);
			currentSprite = &assetPack.spriteSets[GetName(layer->parent)];
			positionParseStatus = NotParsed;
			// initial bbox, to be expanded..
			currentSprite->minPx = { (int)document->width, (int)document->height };
			currentSprite->sizePx = { -(int)document->width, -(int)document->height };
		}

		// direct child of sprite folder, but folder --> base container
		else if (
			layer->parent == currentSpriteFolder &&
			(layer->type == layerType::CLOSED_FOLDER || layer->type == layerType::OPEN_FOLDER)
			) {
			if (!ProcessSpriteMetaData(layer)) return false;
		}

		// grand child of sprite folder, raster layer --> base content, maybe more than 1
		else if (
			layer->parent && layer->parent->parent == currentSpriteFolder &&
			layer->type == layerType::ANY
			) {
			if (!ProcessSpriteBaseOrLightLayerContent(currentSprite->baseLayersData, layer)) return false;
			ExpandPixelBBox(layer);
		}

		// direct child of sprite folder, raster layer --> single base layer, or emission, or lightTex, or corner
		else if (
			layer->parent == currentSpriteFolder && layer->type == layerType::ANY) {
			// single base layer
			if (prevLayer == currentSpriteDivider) {
				if (!ProcessSpriteMetaData(layer)) {
					file.Close();
					return false;
				}
				if (!ProcessSpriteBaseOrLightLayerContent(currentSprite->baseLayersData, layer)) {
					file.Close();
					return false;
				}
				ExpandPixelBBox(layer);
			}
			// corner marker
			else if (tolower(GetName(layer)) == "corner") {
				if (positionParseStatus == ParsedSizeOnly) {
					vec2 cornerPosPx = {
						(layer->right + layer->left) * 0.5f,
						(layer->bottom + layer->top) * 0.5f
					};
					cornerPosPx = cornerPosPx - assetPack.docOriginPx;
					currentSprite->minUnit = PixelPosToUnitPos(cornerPosPx, assetPack.pixelsPerDiagonalUnit);

					positionParseStatus = ParseDone;
				}
			}
			// emission
			else if (tolower(GetName(layer)) == "emission") {
				if (!ProcessSpriteEmissionMaskContent(currentSprite->emissionMaskData, layer)) {
					return false;
				}
			}
			// light tex
			else {
				if (ProcessSpriteBaseOrLightLayerContent(currentSprite->lightLayersData, layer)) {
					currentSprite->lightLayerNames.emplace_back(GetName(layer));
				} else {
					return false;
				}
			}
		}
		prevLayer = layer;
	}

	file.Close();
	return assetPack.isValid();
}


///////////////////////////////////////////////////////////////////////////

#define EXPORT_PPU 100.0f

// returns if write is successful
bool WritePngToDirectory(
	const std::vector<uint8_t>& data,
	uint32_t width, uint32_t height, uint32_t numChannels,
	const std::string& outDir,
	const std::string& fileName,
	float resizeRatio = 1.0f
) {
	std::string fullpath = outDir + "/" + fileName;

	// resize
	uint32_t newWidth = std::ceil(width * resizeRatio);
	uint32_t newHeight = std::ceil(height * resizeRatio);
	std::vector<uint8_t> resizedData(newWidth * newHeight * numChannels);
	stbir_resize_uint8(
		data.data(), width, height, width * numChannels,
		resizedData.data(), newWidth, newHeight, newWidth * numChannels, numChannels);

	// write
	bool success = stbi_write_png(fullpath.c_str(), newWidth, newHeight, numChannels, resizedData.data(), numChannels * newWidth) != 0;
	ASSERT(success)
	return success;
}

std::vector<uint8_t> Crop(const std::vector<uint8_t>& data, uint32_t srcStrideInBytes, uint32_t numChannels, ivec2 minPx, ivec2 sizePx) {
	std::vector<uint8_t> result(sizePx.x * sizePx.y * numChannels);
	uint32_t dstStrideInBytes = sizePx.x * numChannels;
	for (int i = 0; i < sizePx.y; i++) {
		uint32_t srcStart = (minPx.y + i) * srcStrideInBytes + minPx.x * numChannels;
		uint32_t dstStart = i * dstStrideInBytes;
		memcpy(result.data() + dstStart, data.data() + srcStart, dstStrideInBytes);
	}
	return result;
}

struct PivotInfo {
	std::string texPath;
	vec2 pivot;
	Json::Value serialized() const {
		Json::Value result;
		result["texPath"] = texPath;
		result["pivot"] = pivot.serialized();
		return result;
	}
};

struct MaterialInfo {
	std::string name;
	std::string mainTexPath;
	std::string emissionTexPath;
	std::string light0TexPath;
	std::string light0Message = "(none)";
	std::string light1TexPath;
	std::string light1Message = "(none)";
	std::string light2TexPath;
	std::string light2Message = "(none)";
	std::string light3TexPath;
	std::string light3Message = "(none)";
	vec2 basePosition;
	vec2 size;
	Json::Value serialized() const {
		Json::Value result;
		result["name"] = name;
		result["mainTexPath"] = mainTexPath;
		result["emissionTexPath"] = emissionTexPath;
		result["light0TexPath"] = light0TexPath;
		result["light0Message"] = light0Message;
		result["light1TexPath"] = light1TexPath;
		result["light1Message"] = light1Message;
		result["light2TexPath"] = light2TexPath;
		result["light2Message"] = light2Message;
		result["light3TexPath"] = light3TexPath;
		result["light3Message"] = light3Message;
		result["basePosition"] = basePosition.serialized();
		result["size"] = size.serialized();
		return result;
	}
};

std::string SpriteSet::getBaseName() const {
	return JoinTokens(SplitTokens(name));
}

std::string SpriteSet::getBaseTexPath(int index) const {
	return getBaseName() + "_base"+std::to_string(index)+".png";
}

std::string SpriteSet::getLayoutPngPath(const std::string& assetPackName, int index) const {
	std::string result = assetPackName + "&" + getBaseName();
	if (baseLayersData.size() > 1) {
		result += "_part" + std::to_string(index);
	}
	return result + "#.png";
}

std::string SpriteSet::getLightTexPath(int index) const {
	//std::string baseName = JoinTokens(SplitTokens(name));
	return getBaseName() + "_L" + std::to_string(index) +".png";
}

std::string SpriteSet::getEmissionTexPath() const {
	return getBaseName() + "_emission.png";
}

float ComputeResizeRatio(float pixelsPerDiagonalUnit) {
	const float standardPPDU = std::sqrt(2.0f) * EXPORT_PPU;
	const float resizeRatio = standardPPDU / pixelsPerDiagonalUnit;
	return resizeRatio;
}
// a list of all base textures to their anchor points

// also a list of materials, each:
// base tex + all the light textures
// base name

std::string SerializeAssetPack(const AssetPack& assetPack) {
	Json::Value root;

	// pivots (only base layers need them; others are just used as shader textures)
	Json::Value pivots;
	int pivotIdx = 0;
	for (auto& spritePair : assetPack.spriteSets) {
		auto& sprite = spritePair.second;
		vec2 anchorUnit = sprite.minUnit + sprite.sizeUnit / 2;
		vec2 relAnchorPx = UnitPosToPixelPos(anchorUnit, assetPack.pixelsPerDiagonalUnit);
		vec2 anchorPx = relAnchorPx + assetPack.docOriginPx - sprite.minPx;
		vec2 anchorNormalized = {
			anchorPx.x / sprite.sizePx.x,
			1.0f - anchorPx.y / sprite.sizePx.y // since unity wants this reversed
		};
		for (int baseLayerIdx = 0; baseLayerIdx < sprite.baseLayersData.size(); baseLayerIdx++) {
			PivotInfo pivot = {
				sprite.getBaseTexPath(baseLayerIdx),
				anchorNormalized
			};
			pivots[pivotIdx] = pivot.serialized();
			LOG("%s pivot at (%.3f, %.3f)", sprite.getBaseTexPath(baseLayerIdx).c_str(), pivot.pivot.x, pivot.pivot.y)
			pivotIdx++;
		}
	}
	root["pivots"] = pivots;

	// materials
	Json::Value materials;
	int matIdx = 0;
	for (auto& spritePair : assetPack.spriteSets) {
		auto &sprite = spritePair.second;
		for (int baseLayerIdx = 0; baseLayerIdx < sprite.baseLayersData.size(); baseLayerIdx++) {
			MaterialInfo mat;
			mat.name = sprite.getBaseName();
			if (sprite.baseLayersData.size() > 1) {
				mat.name += "_part" + std::to_string(baseLayerIdx);
			}
			LOG("exporting material '%s'..", mat.name.c_str())
			mat.mainTexPath = sprite.getBaseTexPath(baseLayerIdx);
			mat.basePosition = sprite.minUnit;
			mat.size = sprite.sizeUnit;
			for (int lightLayerIdx = 0; lightLayerIdx < sprite.lightLayersData.size(); lightLayerIdx++) {
				if (lightLayerIdx == 0) {
					mat.light0TexPath = sprite.getLightTexPath(0);
					mat.light0Message = sprite.lightLayerNames[0];
				}
				else if (lightLayerIdx == 1) {
					mat.light1TexPath = sprite.getLightTexPath(1);
					mat.light1Message = sprite.lightLayerNames[1];
				}
				else if (lightLayerIdx == 2) {
					mat.light2TexPath = sprite.getLightTexPath(2);
					mat.light2Message = sprite.lightLayerNames[2];
				}
				else if (lightLayerIdx == 3) {
					mat.light3TexPath = sprite.getLightTexPath(3);
					mat.light3Message = sprite.lightLayerNames[3];
				}
				else ASSERT(false)
			}
			// emission
			if (!sprite.emissionMaskData.empty()) {
				mat.emissionTexPath = sprite.getEmissionTexPath();
			}

			materials[matIdx] = mat.serialized();
			matIdx++;
		}
	}
	root["materials"] = materials;

	std::stringstream stream;
	stream << root;
	return stream.str();
}

bool ExportAssetPack(const AssetPack& assetPack, const std::string& destination, const std::string& folderName, int cleanFirst, ExportType exportType) {

	std::string outDir = destination + "\\" + folderName;
	if (exportType == ET_LAYOUT) {
		outDir += " (layout)";
	}

	// remove everything from previous export
	if (cleanFirst > 0 && std::filesystem::exists(outDir)) {
		std::filesystem::remove_all(outDir);
	}

	// from: https://stackoverflow.com/questions/9235679/create-a-directory-if-it-doesnt-exist
	std::filesystem::create_directories(outDir);

	// compute resize ratio
	const float resizeRatio = ComputeResizeRatio(assetPack.pixelsPerDiagonalUnit);
	LOG("resize ratio: %f", resizeRatio)
	//LOG("resize ratio: %.3f", resizeRatio)
	if (resizeRatio > 1) {
		AppendToGUILog({LT_WARNING, "WARNING: resize ratio is " + std::to_string(resizeRatio) + " (>1)"});
	}

	const std::string writeFileError = "ERROR: cannot write file(s). If you are updating existing sprites, make sure to first check them out in perforce.";

	if (exportType == ET_ASSETPACK) {
		for (auto& pair : assetPack.spriteSets) {

			const SpriteSet& sprite = pair.second;
			for (int i = 0; i < sprite.baseLayersData.size(); i++) {
				auto baseRegionData = Crop(sprite.baseLayersData[i], assetPack.docWidth * 4, 4, sprite.minPx, sprite.sizePx);
				if (!WritePngToDirectory(
					baseRegionData, sprite.sizePx.x, sprite.sizePx.y, 4, outDir,
					sprite.getBaseTexPath(i), resizeRatio))
				{
					AppendToGUILog({LT_ERROR, writeFileError});
					return false;
				}
			}
			for (int i = 0; i < sprite.lightLayersData.size(); i++) {
				auto lightTexRegionData = Crop(sprite.lightLayersData[i], assetPack.docWidth * 4, 4, sprite.minPx,
											   sprite.sizePx);
				if (!WritePngToDirectory(
					lightTexRegionData, sprite.sizePx.x, sprite.sizePx.y, 4, outDir,
					sprite.getLightTexPath(i), resizeRatio))
				{
					AppendToGUILog({LT_ERROR, writeFileError});
					return false;
				}
			}
			// emission
			if (!sprite.emissionMaskData.empty()) {
				auto croppedEmissionMask = Crop(sprite.emissionMaskData, assetPack.docWidth, 1, sprite.minPx, sprite.sizePx);
				if (!WritePngToDirectory(
					croppedEmissionMask, sprite.sizePx.x, sprite.sizePx.y, 1, outDir,
					sprite.getEmissionTexPath(), resizeRatio))
				{
					AppendToGUILog({LT_ERROR, writeFileError});
					return false;
				}
			}
		}

		// also export a json
		std::string outstr = SerializeAssetPack(assetPack);

		// write to file
		std::string folderName = outDir.substr(outDir.find_last_of("/\\") + 1);
		if (WriteStringToFile(outDir + "/" + folderName + ".assetpack", outstr)) {
			// write succeeded
		} else {
			AppendToGUILog({LT_ERROR, writeFileError});
			return false;
		}
		return true;
	}
	else if (exportType == ET_LAYOUT) {
		for (auto& pair : assetPack.spriteSets) {
			const SpriteSet& sprite = pair.second;
			for (int i = 0; i < sprite.baseLayersData.size(); i++) {
				auto baseRegionData = Crop(sprite.baseLayersData[i], assetPack.docWidth * 4, 4, sprite.minPx, sprite.sizePx);
				if (!WritePngToDirectory(
					baseRegionData, sprite.sizePx.x, sprite.sizePx.y, 4, outDir,
					sprite.getLayoutPngPath(folderName, i), resizeRatio))
				{
					AppendToGUILog({LT_ERROR, writeFileError});
					return false;
				}
			}
		}
		return true;
	}

	return false;
}
