#pragma once

#include <vector>
#include <unordered_map>
#include <string>
#include "Utils.h"

struct SpriteSet {
	std::string name;
	std::vector<std::vector<uint8_t>> baseLayersData;
	std::vector<std::vector<uint8_t>> lightLayersData;
	std::vector<std::string> lightLayerNames;
	std::vector<uint8_t> emissionMaskData;
	// rel to doc canvas, in pixels
	ivec2 minPx;
	ivec2 sizePx;
	// rel to doc origin, in units (parsed from base layer name)
	vec2 minUnit;
	vec2 sizeUnit;

	std::string getBaseName() const;
	std::string getBaseTexPath(int index) const;
	std::string getLightTexPath(int index) const;
	std::string getEmissionTexPath() const;
};

struct AssetPack {
	std::unordered_map<std::string, SpriteSet> spriteSets;
	vec2 docOriginPx;
	uint32_t docWidth, docHeight;
	float pixelsPerDiagonalUnit = STANDARD_PPDU; // sqrt(2) = 1.41421356237

	bool isValid() const;
};

bool EchoesReadPsdToAssetPack(const std::string& inFile, AssetPack& assetPack);

bool ExportAssetPack(const AssetPack& assetPack, const std::string& outDir, int cleanFirst);

///////////////// windows api specific: