//
// Created by miyehn on 10/3/2023.
//

#include "Log.h"
#include "cxxopts.hpp"

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

#include "AssetPack.h"

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

bool ReadDocument(const std::string& inFile, AssetPack& assetPack) {

	const std::wstring fullPath(inFile.c_str(), inFile.c_str() + inFile.length());
	psd::MallocAllocator allocator;
	psd::NativeFile file(&allocator);

	// open file
	if (!file.OpenRead(fullPath.c_str())) {
		ERR("failed to open file")
		return false;
	}

	// create document
	psd::Document* document = psd::CreateDocument(&file, &allocator);
	if (!document) {
		ERR("failed to create psd document")
		file.Close();
		return false;
	}

	// check color mode
	if (document->colorMode != psd::colorMode::RGB) {
		ERR("psd is not in RGB color mode")
		file.Close();
		return false;
	}

	// check bits per channel
	if (document->bitsPerChannel != 8) {
		ERR("this psd doesn't have 8 bits per channel")
		file.Close();
		return false;
	}

	// read section
	auto section = psd::ParseLayerMaskSection(document, &file, &allocator);
	if (!section) {
		ERR("failed to parse layer mask section")
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
	const Layer* currentSpriteDivider = nullptr;
	const Layer* currentSpriteFolder = nullptr;
	const Layer* prevLayer = nullptr;
	SpriteSet* currentSprite = nullptr;
	std::string currentSpriteName;
	uint32_t layerDataSize = document->width * document->height * 4;
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
		} else {
			WARN("base layer of '%s' has incorrect layer name format! This set of sprites will be anchored at (0, 0)", currentSpriteName.c_str())
			currentSprite->minUnit = {0, 0};
			currentSprite->sizeUnit = {1, 1};
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
			currentSpriteDivider = layer;
			currentSpriteFolder = layer->parent;
			currentSpriteName = GetName(layer->parent);
			currentSprite = &assetPack.spriteSets[GetName(layer->parent)];
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

		// direct child of sprite folder, raster layer --> single base layer, or lightTex
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
			else {
				if (!ProcessSpriteLayerContent(currentSprite->lightLayersData, layer)) return false;
			}
		}
		prevLayer = layer;
	}

	file.Close();
	return true;
}


int main(int argc, const char* argv[]) {

	cxxopts::Options options("EchoesExporter", "todo: put documentation link here");
	options.add_options()
		("f,file", "input psd file, abs path or relative to executable", cxxopts::value<std::string>()/*->default_value("input.psd")*/)
		("o,outdir", "output directory, abs path or relative to executable", cxxopts::value<std::string>()->default_value("output"))
		("h,help", "print help message")
		;
	options.allow_unrecognised_options();

	auto result = options.parse(argc, argv);

	std::string inFile, outDir;
	try {
		inFile = result["file"].as<std::string>();
		outDir = result["outdir"].as<std::string>();
		LOG("in: %s, out: %s", inFile.c_str(), outDir.c_str())
	} catch (std::exception &e) {
		std::cout << options.help() << std::endl;
		return 0;
	}

	//////////////////////////////////////////////////

#if 1
	AssetPack assetPack;
	if (!ReadDocument(inFile, assetPack)) {
		return 1;
	}

	ExportAssetPack(assetPack, outDir);

#endif

	LOG("done.")
	return 0;
}