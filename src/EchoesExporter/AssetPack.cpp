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

std::string GetName(const Layer* layer) {
	std::string name(layer->name.c_str());
	return name;
}

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

bool UnderExport(const Layer* layer) {
	const Layer* itr = layer;
	while (itr) {
		if (GetName(itr) == "export") return true;
		itr = itr->parent;
	}
	return false;
}

bool VisibleInHierarchy(const Layer* layer) {
	const Layer* itr = layer;
	while (itr) {
		if (!itr->isVisible) return false;
		itr = itr->parent;
	}
	return true;
}

const float sqrt2 = std::sqrt(2.0f);
const float sqrt3 = std::sqrt(3.0f);
const vec2 ISO_X = {sqrt2 / 2, sqrt2 / sqrt3 / 2 };
const vec2 ISO_Y = {sqrt2 / 2, -sqrt2 / sqrt3 / 2 };

vec2 ToIsometric(vec2 p) {
	vec2 v0 = ISO_X;
	vec2 v1 = ISO_Y;
	return {
		v0.x * p.x + v1.x * p.y,
		v0.y * p.x + v1.y * p.y
	};
}

vec2 FromIsometric(vec2 p) {
	const float det = ISO_X.x * ISO_Y.y - ISO_Y.x * ISO_X.y;
	vec2 v0 = {
		ISO_Y.y / det,
		-ISO_X.y / det
	};
	vec2 v1 = {
		-ISO_Y.x / det,
		ISO_X.x / det
	};
	return {
		v0.x * p.x + v1.x * p.y,
		v0.y * p.x + v1.y * p.y
	};
}

vec2 PixelPosToUnitPos(vec2 pixelPos, float workingPPDU) {
	vec2 unitPos = FromIsometric(pixelPos);
	return unitPos / (workingPPDU / sqrt(2.0f));
}

vec2 UnitPosToPixelPos(vec2 unitPos, float workingPPDU) {
	vec2 pixelPos = ToIsometric(unitPos);
	return pixelPos * (workingPPDU / sqrt(2.0f));
}

std::vector<std::string> SplitTokens(const std::string& s) {
	std::vector<std::string> tokens;
	std::string cur;
	for (char c : s) {
		if (c == ' ') {
			if (cur.length() > 0) tokens.push_back(cur);
			cur = "";
		} else {
			cur += c;
		}
	}
	if (cur.length() > 0) tokens.push_back(cur);
	return tokens;
}

std::string JoinTokens(const std::vector<std::string> &tokens) {
	std::string result;
	for (auto token : tokens) {
		ASSERT(token.length() > 0)
		token[0] = (char)toupper(token[0]);
		result += token;
	}
	return result;
}

bool EchoesReadPsd(const std::string& inFile, AssetPack& assetPack) {

	const std::wstring fullPath(inFile.c_str(), inFile.c_str() + inFile.length());
	psd::MallocAllocator allocator;
	psd::NativeFile file(&allocator);

	// open file
	if (!file.OpenRead(fullPath.c_str())) {
		AppendGUILog("ERROR: Failed to open file!");
		ERR("failed to open file")
		return false;
	}

	// create document
	psd::Document* document = psd::CreateDocument(&file, &allocator);
	if (!document) {
		AppendGUILog("ERROR: Failed to open file! Was that a PSD document?");
		WARN("failed to create psd document")
		file.Close();
		return false;
	}

	// check color mode
	if (document->colorMode != psd::colorMode::RGB) {
		AppendGUILog("ERROR: This PSD is not in RGB color mode");
		WARN("psd is not in RGB color mode")
		file.Close();
		return false;
	}

	// check bits per channel
	if (document->bitsPerChannel != 8) {
		AppendGUILog("ERROR: This PSD doesn't have 8 bits per channel");
		WARN("this psd doesn't have 8 bits per channel")
		file.Close();
		return false;
	}

	// read section
	auto section = psd::ParseLayerMaskSection(document, &file, &allocator);
	if (!section) {
		AppendGUILog("ERROR: failed to parse layer mask section");
		WARN("failed to parse layer mask section")
		file.Close();
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
		if (layer->parent != nullptr && GetName(layer->parent)=="meta") {
			if (GetName(layer) == "origin") {
				assetPack.docOriginPx = {
					(layer->right + layer->left) * 0.5f,
					(layer->bottom + layer->top) * 0.5f
				};
			} else {
				auto tokens = SplitTokens(GetName(layer));
				if (tokens.size() == 2 && tokens[0] == "ruler") {
					assetPack.pixelsPerDiagonalUnit = (layer->right - layer->left) / std::stof(tokens[1]);
				}
			}
		}
			// export layers: just create something empty and insert into dict for now
		else if (
			VisibleInHierarchy(layer) &&
			(layer->type == layerType::OPEN_FOLDER || layer->type == layerType::CLOSED_FOLDER) &&
			layer->parent != nullptr &&
			GetName(layer->parent)=="export")
		{
			auto name = GetName(layer);
			assetPack.spriteSets[name] = SpriteSet();
			if (layer->layerMask) WARN("folder for sprite '%s' has a layer mask, which will be ignored during export..", name.c_str())
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
		}
		if (blendMode::KeyToEnum(layer->blendModeKey) != blendMode::NORMAL) {
			WARN("base layer of '%s' doesn't have normal blend mode- result might look different.", currentSpriteName.c_str())
		}
		// name
		currentSprite->name = currentSpriteName;

		// unit dims
		auto tokens = SplitTokens(GetName(layer));
		if (tokens.size() == 4) {
			currentSprite->minUnit = {std::stof(tokens[0]), std::stof(tokens[1])};
			currentSprite->sizeUnit = {std::stof(tokens[2]), std::stof(tokens[3])};
			positionParseStatus = ParseDone;
		} else if (tokens.size() == 2) {
			currentSprite->sizeUnit = {std::stof(tokens[0]), std::stof(tokens[1])};
			positionParseStatus = ParsedSizeOnly;
		}
		return true;
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
	auto ProcessSpriteLayerContent = [&](std::vector<std::vector<uint8_t>>& dst, Layer* layer) {
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
	for (int i = 0; i < section->layerCount; i++) {
		Layer* layer = &section->layers[i];
		if (!UnderExport(layer) || (layer->type!=layerType::SECTION_DIVIDER && !VisibleInHierarchy(layer))) continue;

		ExtractLayer(document, &file, &allocator, layer);

		// entering new sprite..
		if (layer->type == layerType::SECTION_DIVIDER &&
			layer->parent && // asset group
			layer->parent->parent && GetName(layer->parent->parent) == "export" && // "export"
			assetPack.spriteSets.find(GetName(layer->parent)) != assetPack.spriteSets.end() // check there is a sprite set
			) {
			// check if last sprite has position info fully parsed, give warning if not:
			if (positionParseStatus != ParseDone) {
				if (positionParseStatus == NotParsed) {
					WARN("failed to parse position and size information for sprite %s; its pivot will be incorrect", currentSprite->name.c_str())
				} else if (positionParseStatus == ParsedSizeOnly) {
					WARN("didn't find position information for sprite %s, its pivot will be incorrect: did you forget to include the 'corner' layer?", currentSprite->name.c_str())
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
			currentSpriteFolder &&
			layer->parent == currentSpriteFolder &&
			(layer->type == layerType::CLOSED_FOLDER || layer->type == layerType::OPEN_FOLDER)
			) {
			if (!ProcessSpriteMetaData(layer)) return false;
		}

			// grand child of sprite folder, raster layer --> base content, maybe more than 1
		else if (
			currentSpriteFolder &&
			layer->parent && layer->parent->parent == currentSpriteFolder &&
			layer->type == layerType::ANY
			) {
			if (!ProcessSpriteLayerContent(currentSprite->baseLayersData, layer)) return false;
			ExpandPixelBBox(layer);
		}

			// direct child of sprite folder, raster layer --> single base layer, or lightTex, or corner
		else if (
			currentSpriteFolder &&
			layer->parent == currentSpriteFolder && layer->type == layerType::ANY) {
			// single base layer
			if (prevLayer == currentSpriteDivider) {
				if (!ProcessSpriteMetaData(layer)) return false;
				if (!ProcessSpriteLayerContent(currentSprite->baseLayersData, layer)) return false;
				ExpandPixelBBox(layer);
			}
				// light tex
			else if (GetName(layer) != "corner") {
				if (ProcessSpriteLayerContent(currentSprite->lightLayersData, layer)) {
					currentSprite->lightLayerNames.emplace_back(GetName(layer));
				} else {
					return false;
				}
			}
				// corner marker
			else {
				ASSERT(GetName(layer) == "corner")
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
		}
		prevLayer = layer;
	}

	file.Close();
	return true;
}


///////////////////////////////////////////////////////////////////////////

#define EXPORT_PPU 100.0f


void WritePngToDirectory(
	const std::vector<uint8_t>& data,
	uint32_t width, uint32_t height,
	const std::string& outDir,
	const std::string& fileName,
	float resizeRatio = 1.0f
) {
	std::string fullpath = outDir + "/" + fileName;

	// resize
	uint32_t newWidth = std::ceil(width * resizeRatio);
	uint32_t newHeight = std::ceil(height * resizeRatio);
	std::vector<uint8_t> resizedData(newWidth * newHeight * 4);
	stbir_resize_uint8(
		data.data(), width, height, width * 4,
		resizedData.data(), newWidth, newHeight, newWidth * 4, 4);

	// write
	EXPECT(stbi_write_png(fullpath.c_str(), newWidth, newHeight, 4, resizedData.data(), 4 * newWidth) != 0, true)
}

std::vector<uint8_t> Crop(const std::vector<uint8_t>& data, uint32_t srcStrideInBytes, ivec2 minPx, ivec2 sizePx) {
	std::vector<uint8_t> result(sizePx.x * sizePx.y * 4);
	uint32_t dstStrideInBytes = sizePx.x * 4;
	for (int i = 0; i < sizePx.y; i++) {
		uint32_t srcStart = (minPx.y + i) * srcStrideInBytes + minPx.x * 4;
		uint32_t dstStart = i * dstStrideInBytes;
		memcpy(result.data() + dstStart, data.data() + srcStart, dstStrideInBytes);
	}
	return result;
}

Json::Value vec2::serialized() const {
	Json::Value result;
	result["x"] = x;
	result["y"] = y;
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

std::string SpriteSet::getLightTexPath(int index) const {
	std::string baseName = JoinTokens(SplitTokens(name));
	return getBaseName() + "_L" + std::to_string(index) +".png";
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

	// pivots
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
	//const float resizeRatio = ComputeResizeRatio(assetPack.pixelsPerDiagonalUnit);
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
			materials[matIdx] = mat.serialized();
			matIdx++;
		}
	}
	root["materials"] = materials;

	std::stringstream stream;
	stream << root;
	return stream.str();
}

bool ExportAssetPack(const AssetPack& assetPack, const std::string& outDir, int cleanFirst) {

	// remove everything from previous export
	if (cleanFirst > 0 && std::filesystem::exists(outDir)) {
		std::filesystem::remove_all(outDir);
	}

	// from: https://stackoverflow.com/questions/9235679/create-a-directory-if-it-doesnt-exist
	std::filesystem::create_directories(outDir);

	// compute resize ratio
	const float resizeRatio = ComputeResizeRatio(assetPack.pixelsPerDiagonalUnit);
	LOG("resize ratio: %.3f", resizeRatio)

	for (auto& pair : assetPack.spriteSets) {

		const SpriteSet& sprite = pair.second;
		for (int i = 0; i < sprite.baseLayersData.size(); i++) {
			auto baseRegionData = Crop(sprite.baseLayersData[i], assetPack.docWidth * 4, sprite.minPx, sprite.sizePx);
			WritePngToDirectory(
				baseRegionData, sprite.sizePx.x, sprite.sizePx.y, outDir,
				sprite.getBaseTexPath(i), resizeRatio);
		}
		for (int i = 0; i < sprite.lightLayersData.size(); i++) {
			auto lightTexRegionData = Crop(sprite.lightLayersData[i], assetPack.docWidth * 4, sprite.minPx, sprite.sizePx);
			WritePngToDirectory(
				lightTexRegionData, sprite.sizePx.x, sprite.sizePx.y, outDir,
				sprite.getLightTexPath(i), resizeRatio);
		}
	}

	/*
	std::string folderName = outDir.substr(outDir.find_last_of("/\\") + 1);
	if (folderName.length() == 0) folderName = outDir;
	LOG("finished saving png files, packing them into %s.zip..", folderName.c_str())

	std::string fullCommand = "tar -cf " + outDir + ".tar " + outDir + "/*";
	system(fullCommand.c_str());
	 */
#if 1
	// also export a json
	std::string outstr = SerializeAssetPack(assetPack);

	// write to file
	std::string folderName = outDir.substr(outDir.find_last_of("/\\") + 1);
	std::ofstream file;
	file.open(outDir + "/" + folderName + ".assetpack");
	if (file.is_open()) {
		file << outstr;
		file.close();
	} else {
		ASSERT(false)
		return false;
	}
	return true;
#else
	return true;
#endif
}
