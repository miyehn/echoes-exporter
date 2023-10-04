#pragma once

#include <vector>
#include <unordered_map>
#include <string>

struct ivec2 {
	int x = 0;
	int y = 0;
};

struct vec2 {
	float x = 0;
	float y = 0;
};

struct SpriteSet {
	std::string name;
	std::vector<uint8_t> baseLayerData;
	std::vector<std::vector<uint8_t>> lightLayersData;
	// rel to doc canvas, in pixels
	ivec2 minPx;
	ivec2 sizePx;
	// rel to doc origin, in units (parsed from base layer name)
	vec2 minUnit;
	vec2 sizeUnit;
};

struct AssetPack {
	std::unordered_map<std::string, SpriteSet> spriteSets;
	vec2 docOriginPx;
	uint32_t docWidth, docHeight;
	float pixelsPerDiagonalUnit; // sqrt(2) = 1.41421356237
};

bool ExportAssetPack(const AssetPack& assetPack, const std::string& outDir);